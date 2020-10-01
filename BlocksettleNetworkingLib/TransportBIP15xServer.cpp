/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "TransportBIP15xServer.h"

#include "FastLock.h"
#include "MessageHolder.h"
#include "StringUtils.h"
#include "SystemFileUtils.h"
#include "BIP15x_Handshake.h"

#include <chrono>

using namespace bs::network;

// A call resetting the encryption-related data for individual connections.
//
// INPUT:  None
// OUTPUT: None
// RETURN: None
void BIP15xPerConnData::reset()
{
   encData_.reset();
   outKeyTimePoint_ = std::chrono::steady_clock::now();
}

// The constructor to use.
//
// INPUT:  Logger object. (const shared_ptr<spdlog::logger>&)
//         Callback for getting a list of trusted clients. (function<vector<string>()>)
//         Ephemeral peer usage. Not recommended. (const bool&)
//         The directory containing the file with the non-ephemeral key. (const std::string)
//         The file with the non-ephemeral key. (const std::string)
//         A flag indicating if the connection will make a key cookie. (bool)
//         A flag indicating if the connection will read a key cookie. (bool)
//         The path to the key cookie to read or write. (const std::string)
// OUTPUT: None
TransportBIP15xServer::TransportBIP15xServer(
   const std::shared_ptr<spdlog::logger> &logger
   , const TrustedClientsCallback& cbTrustedClients
   , bool ephemeralPeers, BIP15xAuthMode authMode
   , const std::string& ownKeyFileDir
   , const std::string& ownKeyFileName, bool makeServerCookie
   , bool readClientCookie, const std::string& cookiePath)
   : TransportBIP15x(logger)
   , ephemeralPeers_(ephemeralPeers), authMode_(authMode)
   , cbTrustedClients_(cbTrustedClients)
   , useClientIDCookie_(readClientCookie)
   , makeServerIDCookie_(makeServerCookie)
   , cookiePath_(cookiePath)
{
   if (ownKeyFileDir.empty() != ownKeyFileName.empty()) {
      throw std::runtime_error("ownKeyFileDir and ownKeyFileName must be set/unset at the same time");
   }

   if (!ephemeralPeers && ownKeyFileName.empty()) {
      throw std::runtime_error("ownKeyFileName must be set when ephemeralPeers is not used");
   }

   if (makeServerIDCookie_ && readClientCookie) {
      throw std::runtime_error("Cannot read client ID cookie and create ID " \
         "cookie at the same time. Connection is incomplete.");
   }

   if (makeServerIDCookie_ && cookiePath.empty()) {
      throw std::runtime_error("ID cookie creation requested but no name " \
         "supplied. Connection is incomplete.");
   }

   if (readClientCookie && cookiePath.empty()) {
      throw std::runtime_error("ID cookie reading requested but no name " \
         "supplied. Connection is incomplete.");
   }

   // In general, load the client key from a special Armory wallet file.
   if (!ephemeralPeers) {
      /*
      TODO:
      Trusted key store conflicts with on disk peer wallet key store.
      Reassess key storage strategy.
      */

      authPeers_ = std::make_unique<AuthorizedPeers>(ownKeyFileDir, ownKeyFileName
         , [] (const std::set<BinaryData> &) { return SecureBinaryData(); });
   }

   /*
   This procedure overwrites all authpeers keys but our own.
   This means the authpeers file effectively only carries our
   own keypair.
   */
   assert(cbTrustedClients_);
   auto trustedClients = cbTrustedClients_();
   {  std::lock_guard<std::mutex> lock(authPeersMutex_);
      bip15x::updatePeerKeys(authPeers_.get(), trustedClients);
   }

   if (makeServerIDCookie_) {
      if (!createCookie()) {
         SPDLOG_LOGGER_ERROR(logger_, "server could not acquire cookie"
            " file. Aborting!");
         throw std::runtime_error("cannot create cookie file");
      }
   }
}

// A specialized server connection constructor with limited options. Used only
// for connections with ephemeral keys
//
// INPUT:  Logger object. (const shared_ptr<spdlog::logger>&)
//         Callback for getting a list of trusted clients. (function<vector<string>()>)
//         A flag indicating if the connection will make a key cookie. (bool)
//         A flag indicating if the connection will read a key cookie. (bool)
//         The path to the key cookie to read or write. (const std::string)
// OUTPUT: None
TransportBIP15xServer::TransportBIP15xServer(const std::shared_ptr<spdlog::logger> &logger
   , const TrustedClientsCallback &cbTrustedClients
   , BIP15xAuthMode authMode
   , const std::string& ownKeyFileDir, const std::string& ownKeyFileName
   , bool makeServerCookie, bool readClientCookie
   , const std::string& cookiePath) :
   TransportBIP15xServer(logger, cbTrustedClients, true, authMode
      , ownKeyFileDir, ownKeyFileName, makeServerCookie, readClientCookie,
      cookiePath)
{}

TransportBIP15xServer::~TransportBIP15xServer() noexcept
{
   // TODO: Send disconnect messages to the clients

   // If it exists, delete the identity cookie.
   if (makeServerIDCookie_) {
      rmCookieFile();
   }
}

void TransportBIP15xServer::rekey(const std::string &clientId)
{
   auto connection = GetConnection(clientId);
   if (connection == nullptr) {
      logger_->error("[TransportBIP15xServer::rekey] can't find connection for {}", BinaryData::fromString(clientId).toHexStr());
      return;
   }

   if (!handshakeCompleted(connection->encData_.get())) {
      logger_->error("[TransportBIP15xServer::rekey] can't rekey {} without BIP151"
         " handshaked completed", BinaryData::fromString(clientId).toHexStr());
      connection->isValid = false;
      return;
   }

   const auto conn = connection->encData_.get();
   BinaryData rekeyData(BIP151PUBKEYSIZE);
   std::memset(rekeyData.getPtr(), 0, BIP151PUBKEYSIZE);

   auto packet = bip15x::MessageBuilder(rekeyData, ArmoryAEAD::HandshakeSequence::Rekey)
      .encryptIfNeeded(conn).build();

   logger_->debug("[TransportBIP15xServer::rekey] rekeying session for {} ({} {})"
      , BinaryData::fromString(clientId).toHexStr(), rekeyData.toHexStr(), packet.toHexStr());

   if (sendDataCb_) {
      sendDataCb_(clientId, packet.toBinStr());
   }

   connection->encData_->rekeyOuterSession();
}

void TransportBIP15xServer::forceTrustedClients(const BIP15xPeers &peers)
{
   forcedTrustedClients_ = std::move(peers);
}

std::unique_ptr<BIP15xPeer> TransportBIP15xServer::getClientKey(const std::string &clientId) const
{
   auto it = socketConnMap_.find(clientId);
   if (it == socketConnMap_.end() || !handshakeCompleted(it->second->encData_.get())) {
      return nullptr;
   }

   const auto &pubKey = bip15x::convertCompressedKey(it->second->encData_->getChosenAuthPeerKey());
   if (pubKey.empty()) {
      SPDLOG_LOGGER_ERROR(logger_, "BIP15XUtils::convertCompressedKey failed");
      return nullptr;
   }
   return std::make_unique<BIP15xPeer>("", pubKey);
}

void TransportBIP15xServer::startHandshake(const std::string &clientID)
{
   bip15x::MessageBuilder payload(ArmoryAEAD::HandshakeSequence::Start);
   const auto &msg = bip15x::Message::parse(payload.build());
   if (!processAEADHandshake(msg, clientID)) {
      SPDLOG_LOGGER_ERROR(logger_, "failed to start AEAD handshake");
      throw std::runtime_error("failed to start AEAD handshake");
   }
}

// The function that processes raw ZMQ connection data. It processes the BIP
// 150/151 handshake (if necessary) and decrypts the raw data.
//
// INPUT:  None
// OUTPUT: None
// RETURN: None
void TransportBIP15xServer::processIncomingData(const std::string &encData
   , const std::string &clientID)
{
   const auto &connData = GetConnection(clientID);
   assert(connData);
   if (!connData->isValid) {
      return;
   }

   auto payload = BinaryData::fromString(encData);

   // Decrypt only if the BIP 151 handshake is complete.
   if (connData->encData_->connectionComplete()) {
      //decrypt packet
      auto result = connData->encData_->decryptPacket(
         payload.getPtr(), payload.getSize(),
         payload.getPtr(), payload.getSize());

      if (result != 0) {
         logger_->error("[TransportBIP15xServer::processIncomingData] packet"
            " {} [{} bytes] decryption failed: {}", payload.toHexStr(), payload.getSize(), result);
         connData->isValid = false;
         clientErrorCb_(clientID, ServerConnectionListener::ClientError::HandshakeFailed, connData->details);
         return;
      }
      payload.resize(payload.getSize() - POLY1305MACLEN);
   }

   // Deserialize packet.
   const auto &msg = bip15x::Message::parse(payload);
   if (!msg.isValid()) {
      if (logger_) {
         logger_->error("[TransportBIP15xServer::processIncomingData] deserialization failed");
      }
      connData->isValid = false;
      clientErrorCb_(clientID, ServerConnectionListener::ClientError::HandshakeFailed, connData->details);
      return;
   }

   if (msg.isForAEADHandshake()) {
      if (!processAEADHandshake(msg, clientID)) {
         reportFatalError(connData);
      }
      return;
   }

   // We can now safely obtain the full message.
   const BinaryDataRef &outMsg = msg.getData();

   // We shouldn't get here but just in case....
   if (connData->encData_->getBIP150State() != BIP150State::SUCCESS) {
      logger_->error("[TransportBIP15xServer::processIncomingData] encryption"
         " handshake is incomplete");
      reportFatalError(connData);
      return;
   }

   // Pass the final data up the chain.
   if (dataReceivedCb_) {
      dataReceivedCb_(clientID, outMsg.toBinStr());
   }
}

// The function processing the BIP 150/151 handshake packets.
//
// INPUT:  The raw handshake packet data. (const BinaryData&)
//         The client ID. (const string&)
// OUTPUT: None
// RETURN: True if success, false if failure.
bool TransportBIP15xServer::processAEADHandshake(const bip15x::Message &msgObj
   , const std::string& clientId)
{
   // Function used to actually send data to the client.
   auto writeToClient = [this, clientId]
      (const BinaryData &msg, uint8_t type, bool encrypt) -> bool
   {
      BIP151Connection* conn = nullptr;
      if (encrypt) {
         auto connection = GetConnection(clientId);
         if (connection == nullptr) {
            logger_->error("[TransportBIP15xServer::processAEADHandshake] no "
               "connection for client {}", BinaryData::fromString(clientId).toHexStr());
            return false;
         }
         conn = connection->encData_.get();
      }

      // Construct the message and fire it down the pipe.
      const auto &packet = bip15x::MessageBuilder(msg, type).encryptIfNeeded(conn).build();
      return sendDataCb_(clientId, packet.toBinStr());
   };

   if (clientId.empty()) {
      logger_->error("[TransportBIP15xServer::processAEADHandshake] empty client ID");
      return false;
   }
   const auto &connection = GetConnection(clientId);
   if (connection == nullptr) {
      logger_->error("[TransportBIP15xServer::processAEADHandshake] no connection"
         " for client {}", bs::toHex(clientId));
      return false;
   }

   switch (msgObj.getAEADType())
   {
   case ArmoryAEAD::HandshakeSequence::Start:
   {
      if (connection->encData_->isOneWayAuth())
      {
         writeToClient(
            connection->encData_->getOwnPubKey(),
            ArmoryAEAD::HandshakeSequence::PresentPubKey,
            false);
      }

      break;
   }

   default:
      break;
   }

   auto status = ArmoryAEAD::BIP15x_Handshake::serverSideHandshake(
      connection->encData_.get(),
      msgObj.getAEADType(), msgObj.getData(),
      writeToClient);

   switch (status)
   {
   case ArmoryAEAD::HandshakeState::StepSuccessful:
      return true;

   case ArmoryAEAD::HandshakeState::Completed:
   {
      connection->outKeyTimePoint_ = std::chrono::steady_clock::now();
      connCb_(clientId, connection->details);
      logger_->info("[TransportBIP15xServer::processAEADHandshake] BIP 150 handshake"
         " with client complete - connection with {} is ready and fully secured"
         , BinaryData::fromString(clientId).toHexStr());

      return true;
   }

   default:
      return false;
   }
}


std::shared_ptr<BIP15xPerConnData> TransportBIP15xServer::GetConnection(const std::string &clientId)
{
   auto it = socketConnMap_.find(clientId);
   if (it == socketConnMap_.end()) {
      return nullptr;
   }
   return it->second;
}

bool TransportBIP15xServer::sendData(const std::string &clientId, const std::string &data)
{
   BIP151Connection* connPtr = nullptr;

   auto connection = GetConnection(clientId);
   if (!connection || !connection->isValid) {
      logger_->error("[TransportBIP15xServer::sendData] can't send {} bytes to "
         "disconnected/invalid connection {}", data.size(), bs::toHex(clientId));
      return false;
   }

   if (connection->encData_->connectionComplete()) {
      connPtr = connection->encData_.get();
   }

   // Check if we need to do a rekey before sending the data.
   if (handshakeCompleted(connection->encData_.get())) {
      bool needsRekey = false;
      auto rightNow = std::chrono::steady_clock::now();

      // Rekey off # of bytes sent or length of time since last rekey.
      if (connPtr->rekeyNeeded(data.size())) {
         needsRekey = true;
      }
      else {
         auto time_sec = std::chrono::duration_cast<std::chrono::seconds>(
            rightNow - connection->outKeyTimePoint_);
         if (time_sec.count() >= bip15x::AEAD_REKEY_INTERVAL_SECS) {
            needsRekey = true;
         }
      }

      if (needsRekey) {
         connection->outKeyTimePoint_ = rightNow;
         rekey(clientId);
      }
   }

   // Encrypt data here if the BIP 150 handshake is complete.
   if (connection->encData_ && connection->encData_->getBIP150State() == BIP150State::SUCCESS) {
      const auto &packet = bip15x::MessageBuilder(BinaryData::fromString(data)
         , bip15x::MsgType::SinglePacket).encryptIfNeeded(connPtr).build();
      return sendDataCb_(clientId, packet.toBinStr());
   }

   logger_->error("[TransportBIP15xServer::sendData] tried to send unencrypted data");
   throw std::runtime_error("trying to send unencrypted data");
}

void TransportBIP15xServer::closeClient(const std::string &clientId)
{
   auto it = socketConnMap_.find(clientId);
   if (it == socketConnMap_.end()) {
      SPDLOG_LOGGER_ERROR(logger_, "connection {} not found", bs::toHex(clientId));
      return;
   }

   const bool wasConnected = handshakeCompleted(it->second->encData_.get());
   socketConnMap_.erase(it);

   SPDLOG_LOGGER_DEBUG(logger_, "connection {} erased, wasConnected: {}", bs::toHex(clientId), wasConnected);

   if (wasConnected) {
      disconnCb_(clientId);
   }
}

// Function used to set the BIP 150/151 handshake data. Called when a connection
// is created.
//
// INPUT:  The client ID. (const string&)
// OUTPUT: None
// RETURN: new or existing connection
void TransportBIP15xServer::addClient(const std::string &clientId, const ServerConnectionListener::Details &details)
{
   SPDLOG_LOGGER_DEBUG(logger_, "adding new connection for client {}"
      , BinaryData::fromString(clientId).toHexStr());
   auto &connection = socketConnMap_[clientId];
   assert(!connection);

   bool oneWayAuth = (authMode_ == BIP15xAuthMode::OneWay) ? true : false;

   auto lbds = getAuthPeerLambda();
   connection = std::make_shared<BIP15xPerConnData>();
   connection->encData_ = std::make_unique<BIP151Connection>(lbds, oneWayAuth);
   connection->outKeyTimePoint_ = std::chrono::steady_clock::now();
   connection->details = details;
   connection->clientId = clientId;

   startHandshake(clientId);
}

void TransportBIP15xServer::reportFatalError(const std::shared_ptr<BIP15xPerConnData> &conn)
{
   if (conn->isValid) {
      conn->isValid = false;
      clientErrorCb_(conn->clientId, ServerConnectionListener::HandshakeFailed, conn->details);
   }
}

bool TransportBIP15xServer::handshakeComplete(const std::string &clientId)
{
   auto connection = GetConnection(clientId);
   if (!connection) {
      return false;
   }
   return (connection->encData_->getBIP150State() == BIP150State::SUCCESS);
}

BIP15xServerParams TransportBIP15xServer::getParams(unsigned port) const
{
   auto addPeersLbd = [this](const BIP15xPeer& peer)
   {
      bip15x::addAuthPeer(authPeers_.get(), peer);
   };

   const auto& pubkey = authPeers_->getOwnPublicKey();
   BinaryData pubkeyBD(pubkey.pubkey, BIP151PUBKEYSIZE);
   return BIP15xServerParams(port, pubkeyBD, addPeersLbd);
}

// Generate a cookie with the server's identity public key.
//
// INPUT:  N/A
// OUTPUT: N/A
// RETURN: True if success, false if failure.
bool TransportBIP15xServer::createCookie()
{
   /*
   We hold the cookie file open with write privileges for the lifetime of
   the server process.
   */

   if (SystemFileUtils::fileExist(cookiePath_)) {
      if (!SystemFileUtils::rmFile(cookiePath_)) {
         logger_->error("[TransportBIP15xServer::createCookie] unable to delete"
            " identity cookie {} - will not write a new one", cookiePath_);
         return false;
      }
   }

   if (cookieFile_ != nullptr) {
      logger_->error("[TransportBIP15xServer::createCookie] identity key file stream"
         " {} is already opened - aborting", cookiePath_);
      return false;
   }

   // Ensure that we only write the compressed key.
   cookieFile_ = std::make_unique<std::ofstream>(cookiePath_, std::ios::out | std::ios::binary);
   if (!cookieFile_->is_open()) {
      logger_->error("[TransportBIP15xServer::createCookie] can't open identity key"
         " {} for writing", cookiePath_);
      cookieFile_.reset();
      return false;
   }
   const BinaryData ourIDKey = getOwnPubKey();
   if (ourIDKey.getSize() != BTC_ECKEY_COMPRESSED_LENGTH) {
      logger_->error("[TransportBIP15xServer::createCookie] server identity key {}"
         " is uncompressed - will not write the identity cookie", cookiePath_);
      cookieFile_.reset();
      return false;
   }

   logger_->debug("[TransportBIP15xServer::createCookie] writing a new identity "
      "cookie {}", cookiePath_);
   cookieFile_->write(getOwnPubKey().getCharPtr(), BTC_ECKEY_COMPRESSED_LENGTH);
   cookieFile_->flush();

   return true;
}

bool TransportBIP15xServer::rmCookieFile()
{
   cookieFile_.reset();
   if (SystemFileUtils::fileExist(cookiePath_)) {
      if (!SystemFileUtils::rmFile(cookiePath_)) {
         logger_->error("[TransportBIP15xServer::rmCookieFile] unable to delete "
            "identity cookie {}", cookiePath_);
         return false;
      }
   }
   return true;
}

bool TransportBIP15xServer::usesCookie() const
{
   return makeServerIDCookie_;
}

bool TransportBIP15xServer::areAuthKeysEphemeral() const
{
   return ephemeralPeers_;
}
