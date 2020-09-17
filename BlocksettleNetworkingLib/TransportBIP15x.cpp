/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "TransportBIP15x.h"

#include <chrono>

#include "BIP150_151.h"
#include "EncryptionUtils.h"
#include "FastLock.h"
#include "FutureValue.h"
#include "MessageHolder.h"
#include "StringUtils.h"
#include "SystemFileUtils.h"
#include "ThreadName.h"
#include "BIP15xMessage.h"
#include "BIP15x_Handshake.h"

using namespace bs::network;


TransportBIP15x::TransportBIP15x(const std::shared_ptr<spdlog::logger> &logger)
   : logger_(logger)
{
   assert(logger_);
   authPeers_ = std::make_unique<AuthorizedPeers>();
}

// static
BinaryData TransportBIP15x::getOwnPubKey_FromKeyFile(const std::string &ownKeyFileDir
   , const std::string &ownKeyFileName)
{
   try {
      AuthorizedPeers authPeers(ownKeyFileDir, ownKeyFileName, [](const std::set<BinaryData> &)
      {
         return SecureBinaryData{};
      });
      return getOwnPubKey_FromAuthPeers(authPeers);
   } catch (const std::exception &) {}
   return {};
}

// static
BinaryData TransportBIP15x::getOwnPubKey_FromAuthPeers(const AuthorizedPeers &authPeers)
{
   try {
      const auto &pubKey = authPeers.getOwnPublicKey();
      return BinaryData(pubKey.pubkey, pubKey.compressed
         ? BTC_ECKEY_COMPRESSED_LENGTH : BTC_ECKEY_UNCOMPRESSED_LENGTH);
   } catch (...) {
      return {};
   }
}

// Get the server's compressed BIP 150 identity public key.
//
// INPUT:  N/A
// OUTPUT: N/A
// RETURN: A buffer with the compressed ECDSA ID pub key. (BinaryData)
BinaryData TransportBIP15x::getOwnPubKey() const
{
   std::lock_guard<std::mutex> lock(authPeersMutex_);
   return getOwnPubKey_FromAuthPeers(*authPeers_);
}

// Add an authorized peer's BIP 150 identity key manually.
void TransportBIP15x::addAuthPeer(const BIP15xPeer &peer)
{
   std::lock_guard<std::mutex> lock(authPeersMutex_);
   bip15x::addAuthPeer(authPeers_.get(), peer);
}

void TransportBIP15x::updatePeerKeys(const BIP15xPeers &peers)
{
   std::lock_guard<std::mutex> lock(authPeersMutex_);
   bip15x::updatePeerKeys(authPeers_.get(), peers);
}

// Get lambda functions related to authorized peers. Copied from Armory.
//
// INPUT:  None
// OUTPUT: None
// RETURN: AuthPeersLambdas object with required lambdas.
AuthPeersLambdas TransportBIP15x::getAuthPeerLambda()
{
   auto getMap = [this](void)->const std::map<std::string, bip15x::PubKey>&
   {
      std::lock_guard<std::mutex> lock(authPeersMutex_);
      return authPeers_->getPeerNameMap();
   };

   auto getPrivKey = [this](const BinaryDataRef& pubkey)->const SecureBinaryData&
   {
      std::lock_guard<std::mutex> lock(authPeersMutex_);
      return authPeers_->getPrivateKey(pubkey);
   };

   auto getAuthSet = [this](void)->const std::set<SecureBinaryData>&
   {
      std::lock_guard<std::mutex> lock(authPeersMutex_);
      return authPeers_->getPublicKeySet();
   };

   return AuthPeersLambdas(getMap, getPrivKey, getAuthSet);
}

bool TransportBIP15x::fail()
{
   isValid_ = false;
   return false;
};

bool TransportBIP15x::handshakeCompleted(const BIP151Connection* connPtr)
{
   if (connPtr == nullptr)
      return false;

   return (connPtr->getBIP150State() == BIP150State::SUCCESS);
}

/////////////////////////////////////////////////////////////////////////////////////////////////
TransportBIP15xClient::TransportBIP15xClient(const std::shared_ptr<spdlog::logger> &logger
   , const BIP15xParams &params)
   : TransportBIP15x(logger), params_(params)
{
   assert(logger_);

   if (!params.ephemeralPeers && (params.ownKeyFileDir.empty() || params.ownKeyFileName.empty())) {
      throw std::runtime_error("Client requested static ID key but no key " \
         "wallet file is specified.");
   }

   outKeyTimePoint_ = std::chrono::steady_clock::now();

   // In general, load the server key from a special Armory wallet file.
   if (!params.ephemeralPeers) {
      authPeers_ = std::make_unique<AuthorizedPeers>(
         params.ownKeyFileDir, params.ownKeyFileName, [](const std::set<BinaryData> &)
      {
         return SecureBinaryData{};
      });
   }
}

TransportBIP15xClient::~TransportBIP15xClient() noexcept
{
   // Need to close connection before socket connection is partially destroyed!
   // Otherwise it might crash in ProcessIncomingData a bit later
   closeConnection();
}

// A function that handles any required rekeys before data is sent.
//
// ***This function must be called before any data is sent on the wire.***
//
// INPUT:  N/A
// OUTPUT: N/A
// RETURN: N/A
void TransportBIP15xClient::rekeyIfNeeded(size_t dataSize)
{
   bool needsRekey = false;
   const auto rightNow = std::chrono::steady_clock::now();

   if (handshakeCompleted()) {
      // Rekey off # of bytes sent or length of time since last rekey.
      if (bip151Connection_->rekeyNeeded(dataSize)) {
         needsRekey = true;
      }
      else {
         auto time_sec = std::chrono::duration_cast<std::chrono::seconds>(
            rightNow - outKeyTimePoint_);
         if (time_sec.count() >= bip15x::AEAD_REKEY_INTERVAL_SECS) {
            needsRekey = true;
         }
      }

      if (needsRekey) {
         outKeyTimePoint_ = rightNow;
         rekey();
      }
   }
}

bool TransportBIP15xClient::sendData(const std::string &data)
{
   if (!bip151Connection_ || (bip151Connection_->getBIP150State() != BIP150State::SUCCESS)) {
      SPDLOG_LOGGER_ERROR(logger_, "transport is not connected, sending packet failed");
      return false;
   }

   rekeyIfNeeded(data.size());

   auto packet = bip15x::MessageBuilder(BinaryData::fromString(data)
      , bip15x::MsgType::SinglePacket).encryptIfNeeded(bip151Connection_.get()).build();
   // An error message is already logged elsewhere if the send fails.
   sendPacket(packet);
   return true;
}

bool TransportBIP15xClient::sendPacket(const BinaryData &packet, bool encrypted)
{
   if (!sendCb_) {
      logger_->error("[TransportBIP15xClient::sendPacket] send callback not set");
      return false;
   }
   if (!isValid()) {
      logger_->error("[TransportBIP15xClient::sendPacket] sending packet in invalid state");
   }
   if (encrypted && (bip151Connection_->getBIP150State() != BIP150State::SUCCESS)) {
      if ((bip151Connection_->getBIP150State() != BIP150State::CHALLENGE1) &&
         (bip151Connection_->getBIP150State() != BIP150State::PROPOSE) &&
         (bip151Connection_->getBIP150State() != BIP150State::REPLY2)) {
         logger_->error("[TransportBIP15xClient::sendPacket] attempt to send encrypted packet"
            " before encryption turned on ({})", (int)bip151Connection_->getBIP150State());
         return fail();
      }
   }

   return sendCb_(packet.toBinStr());
}

void TransportBIP15xClient::rekey()
{
   logger_->debug("[TransportBIP15xClient::rekey] rekeying");

   if (!handshakeCompleted()) {
      logger_->error("[TransportBIP15xClient::rekey] Can't rekey before BIP150 "
         "handshake is complete", __func__);
      fail();
      return;
   }

   BinaryData rekeyData(BIP151PUBKEYSIZE);
   memset(rekeyData.getPtr(), 0, BIP151PUBKEYSIZE);

   auto packet = bip15x::MessageBuilder(rekeyData
      , ArmoryAEAD::HandshakeSequence::Rekey).encryptIfNeeded(bip151Connection_.get()).build();
   logger_->debug("[TransportBIP15xClient::rekey] rekeying session ({} {})"
      , rekeyData.toHexStr(), packet.toHexStr());
   sendPacket(packet);
   bip151Connection_->rekeyOuterSession();
}

// The function that handles raw data coming in from the socket. The data may or
// may not be encrypted.
//
// INPUT:  The raw incoming data. (const string&)
// OUTPUT: None
// RETURN: None
void TransportBIP15xClient::onRawDataReceived(const std::string &rawData)
{
   if (!bip151Connection_ || !isValid()) {
      logger_->error("[TransportBIP15xClient::onRawDataReceived] received {} bytes of"
         " data in disconnected/invalid state", rawData.size());
      return;
   }

   auto payload = BinaryData::fromString(rawData);
   if (!bip151Connection_) {
      logger_->error("[TransportBIP15xClient::onRawDataReceived] received {}-sized"
         " packet in disconnected state", payload.getSize());
      return;
   }
   // Perform decryption if we're ready.
   if (bip151Connection_->connectionComplete()) {
      auto result = bip151Connection_->decryptPacket(
         payload.getPtr(), payload.getSize(),
         payload.getPtr(), payload.getSize());

      if (result != 0) {
         logger_->error("[TransportBIP15xClient::onRawDataReceived] Packet [{} bytes]"
            " decryption failed - error {}", payload.getSize(), result);
         if (socketErrorCb_) {
            socketErrorCb_(DataConnectionListener::ProtocolViolation);
         }
         fail();
         return;
      }
      payload.resize(payload.getSize() - POLY1305MACLEN);
   }
   processIncomingData(payload);
}

void TransportBIP15xClient::openConnection(const std::string &host
   , const std::string &port)
{
   closeConnection();

   host_ = host;
   port_ = port;

   // BIP 151 connection setup. Technically should be per-socket or something
   // similar but data connections will only connect to one machine at a time.
   auto lbds = getAuthPeerLambda();
   bip151Connection_ = std::make_unique<BIP151Connection>(lbds, params_.oneWayAuth);
}

void TransportBIP15xClient::closeConnection()
{
   // If a future obj is still waiting, satisfy it to prevent lockup. This
   // shouldn't happen here but it's an emergency fallback.
   if (serverPubkeyProm_) {
      serverPubkeyProm_->setValue(false);
   }

   SPDLOG_LOGGER_DEBUG(logger_, "[TransportBIP15xClient::closeConnection]");

   bip151Connection_.reset();
}

// The function that processes raw ZMQ connection data. It processes the BIP
// 150/151 handshake (if necessary) and decrypts the raw data.
//
// INPUT:  Reformed message. (const BinaryData&) // TODO: FIX UP MSG
// OUTPUT: None
// RETURN: None
void TransportBIP15xClient::processIncomingData(const BinaryData &payload)
{
   const auto &msg = bip15x::Message::parse(payload);
   if (!msg.isValid()) {
      logger_->error("[TransportBIP15xClient::processIncomingData] deserialization failed");
      if (socketErrorCb_) {
         socketErrorCb_(DataConnectionListener::SerializationFailed);
      }
      return;
   }

   // If we're still handshaking, take the next step. (No fragments allowed.)
   if (msg.isForAEADHandshake()) {
      if (!processAEADHandshake(msg)) {
         logger_->error("[TransportBIP15xClient::processIncomingData] handshake failed");
         if (socketErrorCb_) {
            socketErrorCb_(DataConnectionListener::HandshakeFailed);
         }
      }
      return;
   }

   // We can now safely obtain the full message.
   BinaryDataRef inMsg = msg.getData();

   // We shouldn't get here but just in case....
   if (!bip151Connection_ || (bip151Connection_->getBIP150State() != BIP150State::SUCCESS)) {
      logger_->error("[TransportBIP15xClient::processIncomingData] encryption handshake is incomplete");
      if (bip151Connection_ && socketErrorCb_) {
         socketErrorCb_(DataConnectionListener::HandshakeFailed);
      }
      return;
   }

   // Pass the final data up the chain.
   if (notifyDataCb_) {
      notifyDataCb_(inMsg.toBinStr());
   }
}

// The function processing the BIP 150/151 handshake packets.
//
// INPUT:  The handshake packet.
// OUTPUT: None
// RETURN: True if success, false if failure.
bool TransportBIP15xClient::processAEADHandshake(const bip15x::Message &msgObj)
{
   // Function used to send data out on the wire.
   auto writeData = [this](const BinaryData &payload, uint8_t type, bool encrypt)->bool {
      auto conn = encrypt ? bip151Connection_.get() : nullptr;
      auto packet = bip15x::MessageBuilder(payload, type).encryptIfNeeded(conn).build();
      return sendPacket(packet, (conn != nullptr));
   };

   const std::string &srvId = host_ + ":" + port_;
   switch (msgObj.getAEADType())
   {
   case ArmoryAEAD::HandshakeSequence::PresentPubKey:
   {
      /*
      Packet is the server's pubkey, do we have it?
      This message type isn't handled in the core client AEAD handshake.
      */
      if (!bip151Connection_->isOneWayAuth()) {
         logger_->error("[TransportBIP15xClient::processAEADHandshake] Trying to connect to a "
            "1-way server ({}) as a 2-way client. Aborting!", srvId);
         return false;
      }

      gotKeyAnnounce_ = true;
      if (!bip151Connection_->havePublicKey(msgObj.getData(), srvId)) {
         //we don't have this key, setup key ACK promise
         if (serverPubkeyProm_ != nullptr) {
            logger_->error("[TransportBIP15xClient::processAEADHandshake] server pubkey promise already initialized");
            return false;
         }            
         serverPubkeyProm_ = std::make_shared<FutureValue<bool>>();

         //and prompt the user to verify it
         auto result = verifyNewIDKey(msgObj.getData(), srvId);
         if (result) {
            // Add the key. Old keys aren't deleted automatically. Do it to be safe.
            std::lock_guard<std::mutex> lock(authPeersMutex_);
            authPeers_->eraseName(srvId);
            authPeers_->addPeer(msgObj.getData().copy(), std::vector<std::string>{ srvId });
         }
      } else {
         logger_->warn("[TransportBIP15xClient::processAEADHandshake] server public key was already set");
      }

      return true;
   }

   case ArmoryAEAD::HandshakeSequence::EncInit:
   {
      //reject encinit from a mismatched server
      if (bip151Connection_->isOneWayAuth() && !gotKeyAnnounce_) {
         logger_->error("[TransportBIP15xClient::processAEADHandshake] Trying to connect to a "
            "2-way server ({}) as a 1-way client. Aborting!", srvId);
         return false;
      }

      break;
   }

   default: 
      break;
   }

   //regular client side AEAD handshake processing
   auto status = ArmoryAEAD::BIP15x_Handshake::clientSideHandshake(
      bip151Connection_.get(), srvId,
      msgObj.getAEADType(), msgObj.getData(), 
      writeData);

   switch (status)
   {
   case ArmoryAEAD::HandshakeState::StepSuccessful:
   case ArmoryAEAD::HandshakeState::RekeySuccessful:
      return true;

   case ArmoryAEAD::HandshakeState::Completed:
   {
      outKeyTimePoint_ = std::chrono::steady_clock::now();

      //flag connection as ready
      std::string connType = bip151Connection_->isOneWayAuth() ? "1-way" : "2-way";
      logger_->debug("[TransportBIP15xClient::processAEADHandshake] BIP 150 handshake"
         " with server complete - {} connection to {} is ready and fully secured"
         , connType, srvId);
      if (socketErrorCb_) {
         socketErrorCb_(DataConnectionListener::NoError);
      }

      return true;
   }

   default:
      logger_->error("[TransportBIP15xClient::processAEADHandshake] handshake with {} "
         "failed with error status: {}", srvId, std::to_string(int(status)));
      return false;
   }
}

// Set the callback to be used when asking if the user wishes to accept BIP 150
// identity keys from a server. See the design notes in the header for details.
//
// INPUT:  The callback that will ask the user to confirm the new key. (std::function)
// OUTPUT: N/A
// RETURN: N/A
void TransportBIP15xClient::setKeyCb(const BIP15xNewKeyCb &cb)
{
   if (cb) {
      cbNewKey_ = cb;
   }
   else {
      cbNewKey_ = [this](const std::string &, const std::string, const std::string&
            , const std::shared_ptr<FutureValue<bool>> &prom) {
         SPDLOG_LOGGER_DEBUG(logger_, "no new key callback was set - auto-accepting connections");
         prom->setValue(true);
      };
   }
}

// If the user is presented with a new remote server ID key it doesn't already
// know about, verify the key. A promise will also be used in case any functions
// are waiting on verification results.
//
// INPUT:  The key to verify. (const BinaryDataRef)
//         The server IP address (or host name) and port. (const string)
// OUTPUT: N/A
// RETURN: N/A
bool TransportBIP15xClient::verifyNewIDKey(const BinaryDataRef &key, const std::string &srvId)
{
   logger_->debug("[TransportBIP15xClient::verifyNewIDKey] new key ({}) for server {}"
      " has arrived", key.toHexStr(), srvId);

   if (!cbNewKey_) {
      logger_->error("[TransportBIP15xClient::verifyNewIDKey] no server key callback "
         "is set - aborting handshake");
      if (socketErrorCb_) {
         socketErrorCb_(DataConnectionListener::HandshakeFailed);
      }
      return false;
   }

   // Ask the user if they wish to accept the new identity key.
   // There shouldn't be any old key, at least in authPeerNameSearch
   cbNewKey_({}, key.toHexStr(), srvId, serverPubkeyProm_);
   bool cbResult = false;

   //have we seen the server's pubkey?
   if (serverPubkeyProm_ != nullptr) {
      //if so, wait on the promise
      cbResult = serverPubkeyProm_->waitValue();
      serverPubkeyProm_.reset();
   }

   if (!cbResult) {
      logger_->info("[TransportBIP15xClient::verifyNewIDKey] user refused new server {}"
         " identity key {} - connection refused", srvId, key.toHexStr());
      return false;
   }

   logger_->info("[TransportBIP15xClient::verifyNewIDKey] server {} has new identity "
      "key {} - connection accepted", srvId, key.toHexStr());
   return true;
}

// Get the server's identity public key. Intended for use with local clients.
//
// INPUT:  The accompanying key IP:Port or name. (const string)
// OUTPUT: The buffer that will hold the compressed ID key. (BinaryData)
// RETURN: True if success, false if failure.
bool TransportBIP15xClient::getCookie(const std::string& path, BinaryData& cookieBuf)
{
   if (!usesCookie()) {
      logger_->warn("[TransportBIP15xClient::getCookie] "
         "client is not using a cookie, skipping");
      return false;
   }

   if (!SystemFileUtils::fileExist(path)) {
      logger_->error("[TransportBIP15x::getCookie] client identity cookie {} "
         "doesn't exist - unable to verify server identity", path);
      return false;
   }

   // Ensure that we only read a compressed key.
   std::ifstream cookieFile(path, std::ios::in | std::ios::binary);
   cookieFile.read(cookieBuf.getCharPtr(), BIP151PUBKEYSIZE);
   cookieFile.close();
   if (!(CryptoECDSA().VerifyPublicKeyValid(cookieBuf))) {
      logger_->error("[TransportBIP15x::getCookie] identity key {} "
         "isn't a valid compressed key - unable to verify", cookieBuf.toHexStr());
      return false;
   }
   return true;
}

bool TransportBIP15xClient::addCookieToPeers(
   const std::string& path, const std::string& id)
{
   auto getCookieLbd = [this](const std::string& path
                              , const std::string& id)->bool
   {
      BinaryData cookieKey(static_cast<size_t>(BTC_ECKEY_COMPRESSED_LENGTH));
      if (!getCookie(path, cookieKey)) {
         return false;
      }

      // Add the host and the key to the list of verified peers. Be sure
      // to erase any old keys first.
      std::lock_guard<std::mutex> lock(authPeersMutex_);
      authPeers_->eraseName(id);
      authPeers_->addPeer(cookieKey, std::vector<std::string>({id}));
      return true;
   };

   auto result = getCookieLbd(path, id);
   return result;
}

bool TransportBIP15xClient::areAuthKeysEphemeral() const
{
   return params_.ephemeralPeers;
}

bool TransportBIP15xClient::usesCookie() const
{
   if (!areAuthKeysEphemeral()) {
      return false;
   }

   if (params_.cookie != BIP15xCookie::ReadServer) {
      return false;
   }

   return true;
}

bool TransportBIP15xClient::handshakeCompleted() const
{
   if (bip151Connection_ == nullptr)
      return false;

   return (bip151Connection_->getBIP150State() == BIP150State::SUCCESS);
}