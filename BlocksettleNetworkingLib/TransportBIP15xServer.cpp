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

#include <chrono>

using namespace bs::network;

namespace {
   // How often we should check heartbeats in one heartbeatInterval_.
   const int kHeartbeatsCheckCount = 5;

} // namespace

// A call resetting the encryption-related data for individual connections.
//
// INPUT:  None
// OUTPUT: None
// RETURN: None
void BIP15xPerConnData::reset()
{
   encData_.reset();
   bip150HandshakeCompleted_ = false;
   bip151HandshakeCompleted_ = false;
   outKeyTimePoint_ = std::chrono::steady_clock::now();
}

// The constructor to use.
//
// INPUT:  Logger object. (const shared_ptr<spdlog::logger>&)
//         ZMQ context. (const std::shared_ptr<ZmqContext>&)
//         Per-connection ID. (const uint64_t&)
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
   , const bool& ephemeralPeers, const std::string& ownKeyFileDir
   , const std::string& ownKeyFileName, const bool& makeServerCookie
   , const bool& readClientCookie, const std::string& cookiePath)
   : TransportBIP15x(logger, cookiePath)
   , cbTrustedClients_(cbTrustedClients)
   , useClientIDCookie_(readClientCookie)
   , makeServerIDCookie_(makeServerCookie)
{
   if (!ephemeralPeers && (ownKeyFileDir.empty() || ownKeyFileName.empty())) {
      throw std::runtime_error("Client requested static ID key but no key " \
         "wallet file is specified.");
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
      authPeers_ = std::make_unique<AuthorizedPeers>(ownKeyFileDir, ownKeyFileName
         , [] (const std::set<BinaryData> &) { return SecureBinaryData(); });
   }

   if (makeServerIDCookie_) {
      createCookie();
   }
}

// A specialized server connection constructor with limited options. Used only
// for connections with ephemeral keys that use one-way verification (i.e.,
// clients aren't verified).
//
// INPUT:  Logger object. (const shared_ptr<spdlog::logger>&)
//         ZMQ context. (const std::shared_ptr<ZmqContext>&)
//         Callback for getting a list of trusted clients. (function<vector<string>()>)
//         A flag indicating if the connection will make a key cookie. (bool)
//         A flag indicating if the connection will read a key cookie. (bool)
//         The path to the key cookie to read or write. (const std::string)
// OUTPUT: None
TransportBIP15xServer::TransportBIP15xServer(const std::shared_ptr<spdlog::logger> &logger
   , const TrustedClientsCallback &cbTrustedClients
   , const std::string& ownKeyFileDir, const std::string& ownKeyFileName
   , const bool& makeServerCookie, const bool& readClientCookie
   , const std::string& cookiePath)
   : TransportBIP15x(logger, cookiePath)
   , cbTrustedClients_(cbTrustedClients)
   , useClientIDCookie_(readClientCookie)
   , makeServerIDCookie_(makeServerCookie)
{
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

   if (!ownKeyFileDir.empty() && !ownKeyFileName.empty()) {
      logger_->debug("[TransportBIP15xServer] creating/reading static key in {}/{}"
         , ownKeyFileDir, ownKeyFileName);
      authPeers_ = std::make_unique<AuthorizedPeers>(ownKeyFileDir, ownKeyFileName
         , [](const std::set<BinaryData> &) { return SecureBinaryData{}; });
   }

   if (makeServerIDCookie_) {
      createCookie();
   }
}

TransportBIP15xServer::~TransportBIP15xServer() noexcept
{
   // TODO: Send disconnect messages to the clients

   // If it exists, delete the identity cookie.
   if (makeServerIDCookie_) {
      rmCookieFile();
   }
}

void TransportBIP15xServer::periodicCheck()
{
   checkHeartbeats();
}

void TransportBIP15xServer::rekey(const std::string &clientId)
{
   //XXX : rekey disabled
   return;

   auto connection = GetConnection(clientId);
   if (connection == nullptr) {
      logger_->error("[TransportBIP15xServer::rekey] can't find connection for {}", BinaryData::fromString(clientId).toHexStr());
      return;
   }

   if (!connection->bip151HandshakeCompleted_) {
      logger_->error("[TransportBIP15xServer::rekey] can't rekey {} without BIP151"
         " handshaked completed", BinaryData::fromString(clientId).toHexStr());
      connection->isValid = false;
      return;
   }

   const auto conn = connection->encData_.get();
   BinaryData rekeyData(BIP151PUBKEYSIZE);
   std::memset(rekeyData.getPtr(), 0, BIP151PUBKEYSIZE);

   auto packet = bip15x::MessageBuilder(rekeyData, bip15x::MsgType::AEAD_Rekey)
      .encryptIfNeeded(conn).build();

   logger_->debug("[TransportBIP15xServer::rekey] rekeying session for {} ({} {})"
      , BinaryData::fromString(clientId).toHexStr(), rekeyData.toHexStr(), packet.toHexStr());

   if (sendDataCb_) {
      sendDataCb_(clientId, packet.toBinStr());
   }

   connection->encData_->rekeyOuterSession();
   ++connection->outerRekeyCount_;
}

void TransportBIP15xServer::forceTrustedClients(const BIP15xPeers &peers)
{
   forcedTrustedClients_ = std::move(peers);
}

std::unique_ptr<BIP15xPeer> TransportBIP15xServer::getClientKey(const std::string &clientId) const
{
   auto it = socketConnMap_.find(clientId);
   if (it == socketConnMap_.end() || !it->second->bip150HandshakeCompleted_ || !it->second->bip151HandshakeCompleted_) {
      return nullptr;
   }

   const auto &pubKey = bip15x::convertCompressedKey(it->second->encData_->getChosenAuthPeerKey());
   if (pubKey.empty()) {
      SPDLOG_LOGGER_ERROR(logger_, "ZmqBIP15XUtils::convertCompressedKey failed");
      return nullptr;
   }
   return std::make_unique<BIP15xPeer>("", pubKey);
}

// The function that processes raw ZMQ connection data. It processes the BIP
// 150/151 handshake (if necessary) and decrypts the raw data.
//
// INPUT:  None
// OUTPUT: None
// RETURN: None
void TransportBIP15xServer::processIncomingData(const std::string &encData
   , const std::string &clientID, int socket)
{
   const auto &connData = setBIP151Connection(clientID);
   if (!connData || !connData->isValid) {
      logger_->error("[TransportBIP15xServer::processIncomingData] disconnected/invalid"
         " client {} connection sent {} bytes", encData.size(), bs::toHex(clientID), encData.size());
      if (clientErrorCb_) {
         clientErrorCb_(clientID, "missing connection data");
      }
      return;
   }

   const auto &packets = bip15x::MessageBuilder::parsePackets(BinaryData::fromString(
      accumulBuf_.empty() ? encData : accumulBuf_ + encData));
   if (packets.empty()) {
      logger_->error("[TransportBIP15xServer::processIncomingData] packet deser failed");
      if (clientErrorCb_) {
         clientErrorCb_(clientID, "deserialization failed");
      }
      return;
   }
   if (packets.at(0).empty()) {  // special condition to accumulate more data
      accumulBuf_.append(encData);
      return;
   }
   accumulBuf_.clear();

   for (auto payload : packets) {
      // Decrypt only if the BIP 151 handshake is complete.
      if (connData->bip151HandshakeCompleted_) {
         //decrypt packet
         auto result = connData->encData_->decryptPacket(
            payload.getPtr(), payload.getSize(),
            payload.getPtr(), payload.getSize());

         if (result != 0) {
            logger_->error("[TransportBIP15xServer::processIncomingData] packet"
               " {} [{} bytes] decryption failed: {}", payload.toHexStr(), payload.getSize(), result);
            if (clientErrorCb_) {
               clientErrorCb_(clientID, "packet decryption failed");
            }
            connData->isValid = false;
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
         if (clientErrorCb_) {
            clientErrorCb_(clientID, "deserialization failed");
         }
         return;
      }

      // If we're still handshaking, take the next step. (No fragments allowed.)
      if (msg.getType() == bip15x::MsgType::Heartbeat) {
         UpdateClientHeartbeatTimestamp(clientID);

         const auto &packet = bip15x::MessageBuilder(bip15x::MsgType::Heartbeat)
            .encryptIfNeeded(connData->encData_.get()).build();
         if (sendDataCb_) {
            sendDataCb_(clientID, packet.toBinStr());
         }
         return;
      }

      if (msg.getType() > bip15x::MsgType::AEAD_Threshold) {
         if (!processAEADHandshake(msg, clientID)) {
            if (logger_) {
               logger_->error("[TransportBIP15xServer::processIncomingData] handshake failed");
            }
            if (clientErrorCb_) {
               clientErrorCb_(clientID, "handshake failed");
            }
            if (clientSocketErrorCb_) {
               clientSocketErrorCb_(clientID, ServerConnectionListener::HandshakeFailed, socket);
            }
            return;
         }
         continue;
      }

      // We can now safely obtain the full message.
      const BinaryDataRef &outMsg = msg.getData();

      // We shouldn't get here but just in case....
      if (connData->encData_->getBIP150State() != BIP150State::SUCCESS) {
         if (logger_) {
            logger_->error("[TransportBIP15xServer::processIncomingData] encryption"
               " handshake is incomplete");
         }
         if (clientErrorCb_) {
            clientErrorCb_(clientID, "encryption handshake failure");
         }
         return;
      }

      // Pass the final data up the chain.
      if (dataReceivedCb_) {
         dataReceivedCb_(clientID, outMsg.toBinStr());
      }
   }
}

// The function processing the BIP 150/151 handshake packets.
//
// INPUT:  The raw handshake packet data. (const BinaryData&)
//         The client ID. (const string&)
// OUTPUT: None
// RETURN: True if success, false if failure.
bool TransportBIP15xServer::processAEADHandshake(const bip15x::Message &msgObj
   , const std::string& clientID)
{
   // Function used to actually send data to the client.
   auto writeToClient = [this, clientID]
      (bip15x::MsgType type, const BinaryData &msg, bool encrypt) -> bool
   {
      BIP151Connection* conn = nullptr;
      if (encrypt) {
         auto connection = GetConnection(clientID);
         if (connection == nullptr) {
            logger_->error("[TransportBIP15xServer::processAEADHandshake] no "
               "connection for client {}", BinaryData::fromString(clientID).toHexStr());
            return false;
         }
         conn = connection->encData_.get();
      }

      // Construct the message and fire it down the pipe.
      const auto &packet = bip15x::MessageBuilder(msg, type).encryptIfNeeded(conn).build();
      if (sendDataCb_) {
         return sendDataCb_(clientID, packet.toBinStr());
      }
      return false;
   };

   if (clientID.empty()) {
      logger_->error("[TransportBIP15xServer::processAEADHandshake] empty client ID");
      return false;
   }
   const auto &connection = GetConnection(clientID);
   if (connection == nullptr) {
      logger_->error("[TransportBIP15xServer::processAEADHandshake] no connection"
         " for client {}", bs::toHex(clientID));
      return false;
   }

   if (processAEAD(msgObj, connection->encData_, writeToClient, true)) {
      switch (msgObj.getType())
      {
      case bip15x::MsgType::AEAD_Setup:
      {  // Not handled by common AEAD code
         // If it's a local connection, get a cookie with the client's key.
         if (useClientIDCookie_) {
            // Read the cookie with the key to check.
            if (!addCookieToPeers(clientID)) {
               if (clientErrorCb_) {
                  clientErrorCb_(clientID, "missing client cookie");
               }
               return false;
            }
         }

         //send pubkey message
         if (!writeToClient(bip15x::MsgType::AEAD_PresentPubkey,
            connection->encData_->getOwnPubKey(), false)) {
            logger_->error("[TransportBIP15xServer::processAEADHandshake] AEAD_SETUP: "
               "Response 1 not sent");
         }

         //init bip151 handshake
         BinaryData encinitData(ENCINITMSGSIZE);
         if (connection->encData_->getEncinitData(encinitData.getPtr()
            , ENCINITMSGSIZE, BIP151SymCiphers::CHACHA20POLY1305_OPENSSH) != 0) {
            //failed to init handshake, kill connection
            logger_->error("[TransportBIP15xServer::processAEADHandshake] BIP 150/151"
               " handshake process failed - AEAD_ENCINIT data not obtained");
            connection->isValid = false;
            return false;
         }

         if (!writeToClient(bip15x::MsgType::AEAD_EncInit, encinitData.getRef(), false)) {
            logger_->error("[TransportBIP15xServer::processAEADHandshake] AEAD_SETUP: "
               "Response 2 not sent");
         }
      }
      break;

      case bip15x::MsgType::AEAD_Rekey:
         // Continue after common AEAD handling
         connection->innerRekeyCount_++;
         break;

      case bip15x::MsgType::AEAD_EncInit:
         // Continue after common AEAD handling
         connection->bip151HandshakeCompleted_ = true;
         break;

      case bip15x::MsgType::AuthReply:
         // Continue after common AEAD handling
         if (!forcedTrustedClients_.empty()) {
            const auto &chosenKey = bip15x::convertCompressedKey(connection->encData_->getChosenAuthPeerKey());
            if (chosenKey.empty()) {
               SPDLOG_LOGGER_ERROR(logger_, "invalid choosed public key for forced trusted clients");
               connection->isValid = false;
               return false;
            }

            bool isValid = false;
            for (const auto &client : forcedTrustedClients_) {
               if (client.pubKey() == chosenKey) {
                  isValid = true;
                  break;
               }
            }
            if (!isValid) {
               SPDLOG_LOGGER_ERROR(logger_, "drop connection from unknown client, unexpected public key: {}", chosenKey.toHexStr());
               connection->isValid = false;
               return false;
            }
         }

         //rekey after succesful BIP150 handshake
         connection->encData_->bip150HandshakeRekey();
         connection->bip150HandshakeCompleted_ = true;
         if (connCb_) {
            connCb_(clientID);
         }

         logger_->info("[TransportBIP15xServer::processAEADHandshake] BIP 150 handshake"
            " with client complete - connection with {} is ready and fully secured"
            , BinaryData::fromString(clientID).toHexStr());
         break;

      case bip15x::MsgType::Disconnect:   // Not handled by common AEAD code
         logger_->debug("[TransportBIP15xServer::processAEADHandshake] disconnect"
            " request received from {}", BinaryData::fromString(clientID).toHexStr());
         closeClient(clientID);
         break;

      default: break;
      }
      return true;
   }
   else {
      logger_->error("[TransportBIP15xServer::processAEADHandshake] BIP 150/151"
         " handshake process failed");
      connection->isValid = false;
      return false;
   }
}

// Function used to set the BIP 150/151 handshake data. Called when a connection
// is created.
//
// INPUT:  The client ID. (const string&)
// OUTPUT: None
// RETURN: new or existing connection
std::shared_ptr<BIP15xPerConnData> TransportBIP15xServer::setBIP151Connection(
   const std::string &clientID)
{
   auto connection = GetConnection(clientID);
   if (connection) {
      return connection;
   }

   assert(cbTrustedClients_);
   auto trustedClients = cbTrustedClients_();
   {
      std::lock_guard<std::mutex> lock(authPeersMutex_);
      bip15x::updatePeerKeys(authPeers_.get(), trustedClients);
   }

   auto lbds = getAuthPeerLambda();
   connection = std::make_shared<BIP15xPerConnData>();
   connection->encData_ = std::make_unique<BIP151Connection>(lbds);
   connection->outKeyTimePoint_ = std::chrono::steady_clock::now();

   // XXX add connection
   AddConnection(clientID, connection);

   UpdateClientHeartbeatTimestamp(clientID);

   return connection;
}

bool TransportBIP15xServer::AddConnection(const std::string &clientId
   , const std::shared_ptr<BIP15xPerConnData> &connection)
{
   auto it = socketConnMap_.find(clientId);
   if (it != socketConnMap_.end()) {
      logger_->error("[ZmqBIP15XServerConnection::AddConnection] connection already"
         " saved for {}", BinaryData::fromString(clientId).toHexStr());
      return false;
   }
   socketConnMap_.emplace(clientId, connection);

   logger_->debug("[ZmqBIP15XServerConnection::AddConnection] adding new connection"
      " for client {}", BinaryData::fromString(clientId).toHexStr());
   return true;
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
         "disconnected/invalid connection {}", bs::toHex(clientId));
      return false;
   }

   if (connection->bip151HandshakeCompleted_) {
      connPtr = connection->encData_.get();
   }

   // Check if we need to do a rekey before sending the data.
   if (connection->bip150HandshakeCompleted_) {
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
      if (sendDataCb_) {
         return sendDataCb_(clientId, packet.toBinStr());
      }
   }

   // Send untouched data for straight transmission
   if (sendDataCb_) {
      return sendDataCb_(clientId, data);
   }

   logger_->error("[TransportBIP15xServer::sendData] no sender callback set");
   return false;
}

void TransportBIP15xServer::checkHeartbeats()
{
   auto now = std::chrono::steady_clock::now();
   auto idlePeriod = now - lastHeartbeatCheck_;
   if (idlePeriod < heartbeatInterval_ / kHeartbeatsCheckCount) {
      return;
   }
   lastHeartbeatCheck_ = now;

   if (idlePeriod > heartbeatInterval_ * 2) {
      logger_->debug("[TransportBIP15xServer::checkHeartbeats] hibernation detected,"
         " reset client's last timestamps");
      lastHeartbeats_.clear();
      return;
   }

   std::vector<std::string> timedOutClients;

   for (const auto &hbTime : lastHeartbeats_) {
      const auto diff = now - hbTime.second;
      if (diff > heartbeatInterval_ * 2) {
         timedOutClients.push_back(hbTime.first);
      }
   }

   for (const auto &clientId : timedOutClients) {
      logger_->debug("[TransportBIP15xServer::checkHeartbeats] client {} timed out"
         , BinaryData::fromString(clientId).toHexStr());
      closeClient(clientId);
   }
}

void TransportBIP15xServer::closeClient(const std::string &clientId)
{
   lastHeartbeats_.erase(clientId);

   auto it = socketConnMap_.find(clientId);
   if (it == socketConnMap_.end()) {
      SPDLOG_LOGGER_WARN(logger_, "connection {} not found", bs::toHex(clientId));
      return;
   }

   const bool wasConnected = it->second->bip150HandshakeCompleted_ && it->second->bip151HandshakeCompleted_;
   socketConnMap_.erase(it);

   SPDLOG_LOGGER_DEBUG(logger_, "connection {} erased, wasConnected: {}", bs::toHex(clientId), wasConnected);

   if (wasConnected && disconnCb_) {
      disconnCb_(clientId);
   }
}

void TransportBIP15xServer::UpdateClientHeartbeatTimestamp(const std::string& clientId)
{
   auto currentTime = std::chrono::steady_clock::now();

   auto it = lastHeartbeats_.find(clientId);
   if (it == lastHeartbeats_.end()) {
      lastHeartbeats_.emplace(clientId, currentTime);
      SPDLOG_LOGGER_DEBUG(logger_, "added heartbeat timestamp, clientId: {}, timestamp: {}", bs::toHex(clientId)
         , std::chrono::duration_cast<std::chrono::milliseconds>(currentTime.time_since_epoch()).count());
   } else {
      it->second = currentTime;
   }
}

bool TransportBIP15xServer::getCookie(BinaryData& cookieBuf)
{
   if (!useClientIDCookie_) {
      logger_->error("[TransportBIP15xServer::getClientIDCookie] client identity"
         " cookie requested despite not being available.");
      return false;
   }
   return TransportBIP15x::getCookie(cookieBuf);
}
