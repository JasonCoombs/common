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
#include "TransportBIP15xServer.h"

using namespace bs::network;

namespace
{
   const int HEARTBEAT_PACKET_SIZE = 23;

   const int ControlSocketIndex = 0;
   const int StreamSocketIndex = 1;
   const int MonitorSocketIndex = 2;

} // namespace


BIP15xParams::BIP15xParams()
{
   heartbeatInterval = TransportBIP15xServer::getDefaultHeartbeatInterval();
}

void BIP15xParams::setLocalHeartbeatInterval()
{
   heartbeatInterval = TransportBIP15xServer::getLocalHeartbeatInterval();
}

// The constructor to use.
//
// INPUT:  Logger object. (const shared_ptr<spdlog::logger>&)
//         Params. (BIP15xParams)
// OUTPUT: None
TransportBIP15x::TransportBIP15x(const std::shared_ptr<spdlog::logger> &logger
   , const BIP15xParams &params)
   : logger_(logger)
   , params_(params)
{
   assert(logger_);

   if (!params.ephemeralPeers && (params.ownKeyFileDir.empty() || params.ownKeyFileName.empty())) {
      throw std::runtime_error("Client requested static ID key but no key " \
         "wallet file is specified.");
   }

   if (params_.cookie != BIP15xCookie::NotUsed && params_.cookiePath.empty()) {
      throw std::runtime_error("ID cookie creation requested but no name " \
         "supplied. Connection is incomplete.");
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
   else {
      authPeers_ = std::make_unique<AuthorizedPeers>();
   }

   if (params_.cookie == BIP15xCookie::MakeClient) {
      genBIPIDCookie();
   }
   lastHeartbeatSend_ = std::chrono::steady_clock::now();
}

TransportBIP15x::~TransportBIP15x() noexcept
{
   // If it exists, delete the identity cookie.
   if (params_.cookie == BIP15xCookie::MakeClient) {
//      const string absCookiePath =
//         SystemFilePaths::appDataLocation() + "/" + bipIDCookieName_;
      if (SystemFileUtils::fileExist(params_.cookiePath)) {
         if (!SystemFileUtils::rmFile(params_.cookiePath)) {
            logger_->error("[~TransportBIP15x] Unable to delete client identity"
               " cookie {}", params_.cookiePath);
         }
      }
   }

   // Need to close connection before socket connection is partially destroyed!
   // Otherwise it might crash in ProcessIncomingData a bit later
   closeConnection();
}

// Get lambda functions related to authorized peers. Copied from Armory.
//
// INPUT:  None
// OUTPUT: None
// RETURN: AuthPeersLambdas object with required lambdas.
AuthPeersLambdas TransportBIP15x::getAuthPeerLambda() const
{
   auto getMap = [this](void) -> const std::map<std::string, bip15x::PubKey>& {
      std::lock_guard<std::mutex> lock(authPeersMutex_);
      return authPeers_->getPeerNameMap();
   };

   auto getPrivKey = [this](const BinaryDataRef& pubkey) -> const SecureBinaryData& {
      std::lock_guard<std::mutex> lock(authPeersMutex_);
      return authPeers_->getPrivateKey(pubkey);
   };

   auto getAuthSet = [this](void) -> const std::set<SecureBinaryData>& {
      std::lock_guard<std::mutex> lock(authPeersMutex_);
      return authPeers_->getPublicKeySet();
   };

   return AuthPeersLambdas(getMap, getPrivKey, getAuthSet);
}

// A function that handles any required rekeys before data is sent.
//
// ***This function must be called before any data is sent on the wire.***
//
// INPUT:  N/A
// OUTPUT: N/A
// RETURN: N/A
void TransportBIP15x::rekeyIfNeeded(size_t dataSize)
{
   bool needsRekey = false;
   const auto rightNow = std::chrono::steady_clock::now();

   if (bip150HandshakeCompleted_) {
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

void TransportBIP15x::startConnection()
{
   connectionStarted_ = std::chrono::steady_clock::now();;
}

long TransportBIP15x::pollTimeoutMS() const
{
   const long toMS = isConnected_ ? (params_.heartbeatInterval / std::chrono::milliseconds(1) / 5)
      : (params_.connectionTimeout / std::chrono::milliseconds(1) / 10);
   return std::max((long)1, toMS);
}

bool TransportBIP15x::sendData(const std::string &data)
{
   if (bip151Connection_->getBIP150State() != BIP150State::SUCCESS) {
      return false;
   }

   auto packet = bip15x::MessageBuilder(BinaryData::fromString(data)
      , bip15x::MsgType::SinglePacket).encryptIfNeeded(bip151Connection_.get()).build();
   // An error message is already logged elsewhere if the send fails.
   sendPacket(packet);
   return true;
}

void TransportBIP15x::sendPacket(const BinaryData &packet, bool encrypted)
{
   if (!sendCb_) {
      logger_->error("[TransportBIP15x::sendPacket] send callback not set");
      return;
   }
   if (encrypted && (bip151Connection_->getBIP150State() != BIP150State::SUCCESS)) {
      if ((bip151Connection_->getBIP150State() != BIP150State::CHALLENGE1) &&
         (bip151Connection_->getBIP150State() != BIP150State::PROPOSE) &&
         (bip151Connection_->getBIP150State() != BIP150State::REPLY2)) {
         logger_->error("[TransportBIP15x::sendPacket] attempt to send encrypted packet"
            " before encryption turned on ({})", (int)bip151Connection_->getBIP150State());
         return;
      }
   }

   if (encrypted) {
      rekeyIfNeeded(packet.getSize());
   }

   if (!sendCb_(packet.toBinStr())) {
      logger_->error("[TransportBIP15x::sendPacket] failed to send {} bytes"
         , packet.getSize());
   }
}

void TransportBIP15x::rekey()
{
   logger_->debug("[TransportBIP15x::rekey] rekeying");

   if (!bip150HandshakeCompleted_) {
      logger_->error("[TransportBIP15x::rekey] Can't rekey before BIP150 "
         "handshake is complete", __func__);
      return;
   }

   BinaryData rekeyData(BIP151PUBKEYSIZE);
   memset(rekeyData.getPtr(), 0, BIP151PUBKEYSIZE);

   auto packet = bip15x::MessageBuilder(rekeyData.getRef()
      , bip15x::MsgType::AEAD_Rekey).encryptIfNeeded(bip151Connection_.get()).build();
   sendPacket(packet);
   bip151Connection_->rekeyOuterSession();
   ++outerRekeyCount_;
}

// A function that is used to trigger heartbeats. Required because ZMQ is unable
// to tell, via a data socket connection, when a client has disconnected.
//
// INPUT:  N/A
// OUTPUT: N/A
// RETURN: N/A
void TransportBIP15x::triggerHeartbeatCheck()
{
   if (!bip151HandshakeCompleted_) {
      return;
   }

   const auto now = std::chrono::steady_clock::now();
   const auto idlePeriod = now - lastHeartbeatSend_;
   if (idlePeriod < params_.heartbeatInterval) {
      return;
   }
   lastHeartbeatSend_ = now;

   // If a rekey is needed, rekey before encrypting. Estimate the size of the
   // final packet first in order to get the # of bytes transmitted.
   rekeyIfNeeded(HEARTBEAT_PACKET_SIZE);

   auto packet = bip15x::MessageBuilder(bip15x::MsgType::Heartbeat)
      .encryptIfNeeded(bip151Connection_.get()).build();

   // An error message is already logged elsewhere if the send fails.
   // sendPacket already sets the timestamp.
   sendPacket(packet);

   if (idlePeriod > params_.heartbeatInterval * 2) {
      logger_->debug("[ZmqBIP15XDataConnection:{}] hibernation detected, reset server's last timestamp", __func__);
      lastHeartbeatReply_ = now;
      return;
   }

   auto lastHeartbeatDiff = now - lastHeartbeatReply_;
   if (socketErrorCb_ && (lastHeartbeatDiff > params_.heartbeatInterval * 2)) {
      socketErrorCb_(DataConnectionListener::HeartbeatWaitFailed);
   }
}

// static
BinaryData TransportBIP15x::getOwnPubKey(const std::string &ownKeyFileDir, const std::string &ownKeyFileName)
{
   return TransportBIP15xServer::getOwnPubKey(ownKeyFileDir, ownKeyFileName);
}

// static
BinaryData TransportBIP15x::getOwnPubKey(const AuthorizedPeers &authPeers)
{
   return TransportBIP15xServer::getOwnPubKey(authPeers);
}

// Kick off the BIP 151 handshake. This is the first function to call once the
// unencrypted connection is established.
//
// INPUT:  None
// OUTPUT: None
// RETURN: True if success, false if failure.
bool TransportBIP15x::startBIP151Handshake()
{
   const auto &packet = bip15x::MessageBuilder(bip15x::MsgType::AEAD_Setup).build();
   sendPacket(packet, false);
   return true;
}

// The function that handles raw data coming in from the socket. The data may or
// may not be encrypted.
//
// INPUT:  The raw incoming data. (const string&)
// OUTPUT: None
// RETURN: None
void TransportBIP15x::onRawDataReceived(const std::string &rawData)
{
   if (!bip151Connection_) {
      logger_->error("[TransportBIP15x::onRawDataReceived] received {} bytes of"
         " data in disconnected state", rawData.size());
      return;
   }

   const auto &packets = bip15x::MessageBuilder::parsePackets(
      BinaryData::fromString(accumulBuf_.empty() ? rawData : accumulBuf_ + rawData));
   if (packets.empty()) {
      logger_->error("[TransportBIP15x::processIncomingData] packet deser failed"
         " for {} [{}]", bs::toHex(rawData), rawData.size());
      if (socketErrorCb_) {
         socketErrorCb_(DataConnectionListener::SerializationFailed);
      }
      return;
   }
   if (packets.at(0).empty()) {  // special condition to accumulate more data
      accumulBuf_.append(rawData);
      return;
   }
   accumulBuf_.clear();

   for (auto payload : packets) {
      if (!bip151Connection_) {
         logger_->error("[TransportBIP15x::onRawDataReceived] received {}-sized"
            " packet in disconnected state", payload.getSize());
         return;
      }
      // Perform decryption if we're ready.
      if (bip151Connection_->connectionComplete()) {
         auto result = bip151Connection_->decryptPacket(
            payload.getPtr(), payload.getSize(),
            payload.getPtr(), payload.getSize());

         if (result != 0) {
            logger_->error("[TransportBIP15x::onRawDataReceived] Packet [{} bytes]"
               " decryption failed - error {}", payload.getSize(), result);
            if (socketErrorCb_) {
               socketErrorCb_(DataConnectionListener::ProtocolViolation);
            }
            return;
         }
         payload.resize(payload.getSize() - POLY1305MACLEN);
      }
      processIncomingData(payload);
   }
}

void TransportBIP15x::openConnection(const std::string &host, const std::string &port)
{
   host_ = host;
   port_ = port;

   // BIP 151 connection setup. Technically should be per-socket or something
   // similar but data connections will only connect to one machine at a time.
   auto lbds = getAuthPeerLambda();
   bip151Connection_ = std::make_unique<BIP151Connection>(lbds);

   isConnected_ = false;
   serverSendsHeartbeat_ = false;
}

void TransportBIP15x::closeConnection()
{
   // Do not call from callbacks!
   // If a future obj is still waiting, satisfy it to prevent lockup. This
   // shouldn't happen here but it's an emergency fallback.
   if (serverPubkeyProm_) {
      serverPubkeyProm_->setValue(false);
   }
   sendDisconnect();

   SPDLOG_LOGGER_DEBUG(logger_, "[TransportBIP15x::closeConnection]");

   bip151Connection_.reset();
   bip150HandshakeCompleted_ = false;
   bip151HandshakeCompleted_ = false;
}

// The function that processes raw ZMQ connection data. It processes the BIP
// 150/151 handshake (if necessary) and decrypts the raw data.
//
// INPUT:  Reformed message. (const BinaryData&) // TODO: FIX UP MSG
// OUTPUT: None
// RETURN: None
void TransportBIP15x::processIncomingData(const BinaryData &payload)
{
   const auto &msg = bip15x::Message::parse(payload);
   if (!msg.isValid()) {
      logger_->error("[TransportBIP15x::processIncomingData] deserialization failed");
      if (socketErrorCb_) {
         socketErrorCb_(DataConnectionListener::SerializationFailed);
      }
      return;
   }

   if (msg.getType() == bip15x::MsgType::Heartbeat) {
      lastHeartbeatReply_ = std::chrono::steady_clock::now();
      serverSendsHeartbeat_ = true;
      return;
   }

   // If we're still handshaking, take the next step. (No fragments allowed.)
   if (msg.getType() > bip15x::MsgType::AEAD_Threshold) {
      if (!processAEADHandshake(msg)) {
         logger_->error("[TransportBIP15x::processIncomingData] handshake failed");
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
      logger_->error("[socketErrorCb_] Encryption handshake is incomplete");
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
// INPUT:  The handshake packet. (const ZmqBIP15XMsg&)
// OUTPUT: None
// RETURN: True if success, false if failure.
bool TransportBIP15x::processAEADHandshake(const bip15x::Message &msgObj)
{
   // Function used to send data out on the wire.
   auto writeData = [this](BinaryData& payload, bip15x::MsgType type, bool encrypt) {
      auto conn = encrypt ? bip151Connection_.get() : nullptr;
      auto packet = bip15x::MessageBuilder(payload, type).encryptIfNeeded(conn).build();
      sendPacket(packet, (conn != nullptr));
   };

   const std::string &srvId = host_ + ":" + port_;

   // Read the message, get the type, and process as needed. Code mostly copied
   // from Armory.
   auto msgbdr = msgObj.getData();
   switch (msgObj.getType()) {
   case bip15x::MsgType::AEAD_PresentPubkey:
   {
      /*packet is server's pubkey, do we have it?*/
      //init server promise
      serverPubkeyProm_ = std::make_shared<FutureValue<bool>>();

      // If it's a local connection, get a cookie with the server's key.
      if (params_.cookie == BIP15xCookie::ReadServer) {
         // Read the cookie with the key to check.
         BinaryData cookieKey(static_cast<size_t>(BTC_ECKEY_COMPRESSED_LENGTH));
         if (!getServerIDCookie(cookieKey)) {
            return false;
         }
         else {
            // Add the host and the key to the list of verified peers. Be sure
            // to erase any old keys first.
            const std::vector<std::string> keyName = { srvId };

            std::lock_guard<std::mutex> lock(authPeersMutex_);
            authPeers_->eraseName(srvId);
            authPeers_->addPeer(cookieKey, keyName);
         }
      }

      // If we don't have the key already, we may ask the the user if they wish
      // to continue. (Remote signer only.)
      if (!bip151Connection_->havePublicKey(msgbdr, srvId)) {
         //we don't have this key, call user prompt lambda
         if (verifyNewIDKey(msgbdr, srvId)) {
            // Add the key. Old keys aren't deleted automatically. Do it to be safe.
            std::lock_guard<std::mutex> lock(authPeersMutex_);
            authPeers_->eraseName(srvId);
            authPeers_->addPeer(msgbdr.copy(), std::vector<std::string>{ srvId });
         }
      }
      else {
         //set server key promise
         if (serverPubkeyProm_) {
            serverPubkeyProm_->setValue(true);
         }
         else {
            logger_->warn("[TransportBIP15x::processAEADHandshake] server public key was already set");
         }
      }

      break;
   }

   case bip15x::MsgType::AEAD_EncInit:
   {
      if (bip151Connection_->processEncinit(msgbdr.getPtr(), msgbdr.getSize()
         , false) != 0) {
         logger_->error("[TransportBIP15x::processAEADHandshake] BIP 150/151 "
            "handshake process failed - AEAD_ENCINIT not processed");
         return false;
      }

      //valid encinit, send client side encack
      BinaryData encackPayload(BIP151PUBKEYSIZE);
      if (bip151Connection_->getEncackData(encackPayload.getPtr()
         , BIP151PUBKEYSIZE) != 0) {
         logger_->error("[TransportBIP15x::processAEADHandshake] BIP 150/151 "
            "handshake process failed - AEAD_ENCACK data not obtained");
         return false;
      }
      writeData(encackPayload, bip15x::MsgType::AEAD_EncAck, false);

      //start client side encinit
      BinaryData encinitPayload(ENCINITMSGSIZE);
      if (bip151Connection_->getEncinitData(encinitPayload.getPtr()
         , ENCINITMSGSIZE, BIP151SymCiphers::CHACHA20POLY1305_OPENSSH) != 0) {
         logger_->error("[TransportBIP15x::processAEADHandshake] BIP 150/151 "
            "handshake process failed - AEAD_ENCINIT data not obtained");
         return false;
      }
      writeData(encinitPayload, bip15x::MsgType::AEAD_EncInit, false);
   }
   break;

   case bip15x::MsgType::AEAD_EncAck:
   {
      if (bip151Connection_->processEncack(msgbdr.getPtr(), msgbdr.getSize()
         , true) == -1) {
         logger_->error("[TransportBIP15x::processAEADHandshake] BIP 150/151 "
            "handshake process failed - AEAD_ENCACK not processed");
         return false;
      }

      // Do we need to check the server's ID key?
      if (serverPubkeyProm_ != nullptr) {
         //if so, wait on the promise
         bool result = serverPubkeyProm_->waitValue();

         if (result) {
            serverPubkeyProm_.reset();
         }
         else {
            logger_->error("[TransportBIP15x::processAEADHandshake] BIP 150/151"
               " handshake process failed - AEAD_ENCACK - Server public key not verified");
            return false;
         }
      }

      //bip151 handshake completed, time for bip150
      BinaryData authchallengeBuf(BIP151PRVKEYSIZE);
      if (bip151Connection_->getAuthchallengeData(
         authchallengeBuf.getPtr(), authchallengeBuf.getSize(), srvId
         , true //true: auth challenge step #1 of 6
         , false) != 0) { //false: have not processed an auth propose yet
         logger_->error("[TransportBIP15x::processAEADHandshake] BIP 150/151 "
            "handshake process failed - AUTH_CHALLENGE data not obtained");
         return false;
      }
      writeData(authchallengeBuf, bip15x::MsgType::AuthChallenge, true);
      bip151HandshakeCompleted_ = true;
   }
   break;

   case bip15x::MsgType::AEAD_Rekey:
   {
      // Rekey requests before auth are invalid.
      if (bip151Connection_->getBIP150State() != BIP150State::SUCCESS) {
         logger_->error("[TransportBIP15x::processAEADHandshake] BIP 150/151 "
            "handshake process failed - Not ready to rekey");
         return false;
      }

      // If connection is already setup, we only accept rekey enack messages.
      if (bip151Connection_->processEncack(msgbdr.getPtr(), msgbdr.getSize()
         , false) == -1) {
         logger_->error("[TransportBIP15x::processAEADHandshake] BIP 150/151 "
            "handshake process failed - AEAD_REKEY not processed");
         return false;
      }
      ++innerRekeyCount_;
   }
   break;

   case bip15x::MsgType::AuthReply:
   {
      if (bip151Connection_->processAuthreply(msgbdr.getPtr(), msgbdr.getSize()
         , true //true: step #2 out of 6
         , false) != 0) { //false: haven't seen an auth challenge yet
         logger_->error("[TransportBIP15x::processAEADHandshake] BIP 150/151 "
            "handshake process failed - AUTH_REPLY not processed");
         return false;
      }

      BinaryData authproposeBuf(BIP151PRVKEYSIZE);
      if (bip151Connection_->getAuthproposeData(
         authproposeBuf.getPtr(),
         authproposeBuf.getSize()) != 0) {
         logger_->error("[TransportBIP15x::processAEADHandshake] BIP 150/151 "
            "handshake process failed - AUTH_PROPOSE data not obtained");
         return false;
      }
      writeData(authproposeBuf, bip15x::MsgType::AuthPropose, true);
   }
   break;

   case bip15x::MsgType::AuthChallenge:
   {
      bool goodChallenge = true;
      auto challengeResult =
         bip151Connection_->processAuthchallenge(msgbdr.getPtr()
            , msgbdr.getSize(), false); //true: step #4 of 6

      if (challengeResult == -1) {  //auth fail, kill connection
         logger_->error("[TransportBIP15x::processAEADHandshake] BIP 150/151 "
            "handshake process failed - AUTH_CHALLENGE not processed");
         return false;
      }

      if (challengeResult == 1) {
         goodChallenge = false;
      }

      BinaryData authreplyBuf(BIP151PRVKEYSIZE * 2);
      auto validReply = bip151Connection_->getAuthreplyData(
         authreplyBuf.getPtr(), authreplyBuf.getSize()
         , false //true: step #5 of 6
         , goodChallenge);

      if (validReply != 0) {  // auth setup failure, kill connection
         logger_->error("[TransportBIP15x::processAEADHandshake] BIP 150/151 "
            "handshake process failed - AUTH_REPLY data not obtained");
         return false;
      }
      writeData(authreplyBuf, bip15x::MsgType::AuthReply, true);

      // Rekey.
      bip151Connection_->bip150HandshakeRekey();
      bip150HandshakeCompleted_ = true;

      auto now = std::chrono::steady_clock::now();
      outKeyTimePoint_ = now;
      lastHeartbeatReply_ = now;
      lastHeartbeatSend_ = now;

      logger_->debug("[TransportBIP15x::processAEADHandshake] BIP 150 handshake"
         " with server complete - connection to {} is ready and fully secured", srvId);
      if (socketErrorCb_) {
         socketErrorCb_(DataConnectionListener::NoError);
      }
   }
   break;

   default:
      logger_->error("[TransportBIP15x::processAEADHandshake] unknown message "
         "type {}", (int)msgObj.getType());
      return false;
   }
   return true;
}

// Set the callback to be used when asking if the user wishes to accept BIP 150
// identity keys from a server. See the design notes in the header for details.
//
// INPUT:  The callback that will ask the user to confirm the new key. (std::function)
// OUTPUT: N/A
// RETURN: N/A
void TransportBIP15x::setKeyCb(const BIP15xNewKeyCb &cb)
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

// Add an authorized peer's BIP 150 identity key manually.
//
// INPUT:  The authorized key. (const BinaryData)
//         The server IP address (or host name) and port. (const string)
// OUTPUT: N/A
// RETURN: N/A
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

// If the user is presented with a new remote server ID key it doesn't already
// know about, verify the key. A promise will also be used in case any functions
// are waiting on verification results.
//
// INPUT:  The key to verify. (const BinaryDataRef)
//         The server IP address (or host name) and port. (const string)
// OUTPUT: N/A
// RETURN: N/A
bool TransportBIP15x::verifyNewIDKey(const BinaryDataRef &key, const std::string &srvId)
{
   if (params_.cookie == BIP15xCookie::ReadServer) {
      // If we get here, it's because the cookie add failed or the cookie was
      // incorrect. Satisfy the promise to prevent lockup.
      logger_->error("[{}] Server ID key cookie could not be verified", __func__);
      serverPubkeyProm_->setValue(false);
      if (socketErrorCb_) {
         socketErrorCb_(DataConnectionListener::HandshakeFailed);
      }
      return false;
   }

   logger_->debug("[TransportBIP15x::verifyNewIDKey] new key ({}) for server {}"
      " has arrived", key.toHexStr(), srvId);

   if (!cbNewKey_) {
      logger_->error("[TransportBIP15x::verifyNewIDKey] no server key callback "
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
      logger_->info("[TransportBIP15x::verifyNewIDKey] user refused new server {}"
         " identity key {} - connection refused", srvId, key.toHexStr());
      return false;
   }

   logger_->info("[TransportBIP15x::verifyNewIDKey] server {} has new identity "
      "key {} - connection accepted", srvId, key.toHexStr());
   return true;
}

// Get the signer's identity public key. Intended for use with local signers.
//
// INPUT:  The accompanying key IP:Port or name. (const string)
// OUTPUT: The buffer that will hold the compressed ID key. (BinaryData)
// RETURN: True if success, false if failure.
bool TransportBIP15x::getServerIDCookie(BinaryData &cookieBuf)
{
   if (params_.cookie != BIP15xCookie::ReadServer) {
      return false;
   }

   if (!SystemFileUtils::fileExist(params_.cookiePath)) {
      logger_->error("[TransportBIP15x::getServerIDCookie] server identity cookie"
         " ({}) doesn't exist - unable to verify server identity.", params_.cookiePath);
      return false;
   }

   // Ensure that we only read a compressed key.
   std::ifstream cookieFile(params_.cookiePath, std::ios::in | std::ios::binary);
   cookieFile.read(cookieBuf.getCharPtr(), BIP151PUBKEYSIZE);
   cookieFile.close();
   if (!(CryptoECDSA().VerifyPublicKeyValid(cookieBuf))) {
      logger_->error("[TransportBIP15x::getServerIDCookie] server identity key "
         "({}) isn't a valid compressed key - unable to verify server identity."
         , cookieBuf.toHexStr());
      return false;
   }
   return true;
}

// Generate a cookie with the client's identity public key.
//
// INPUT:  N/A
// OUTPUT: N/A
// RETURN: True if success, false if failure.
bool TransportBIP15x::genBIPIDCookie()
{
   if (params_.cookie != BIP15xCookie::MakeClient) {
      logger_->error("[TransportBIP15x::genBIPIDCookie] ID cookie creation "
         "requested but not allowed");
      return false;
   }

   if (SystemFileUtils::fileExist(params_.cookiePath)) {
      if (!SystemFileUtils::rmFile(params_.cookiePath)) {
         logger_->error("[TransportBIP15x::genBIPIDCookie] Unable to delete client"
            " identity cookie ({}) - will not write a new cookie", params_.cookiePath);
         return false;
      }
   }

   // Ensure that we only write the compressed key.
   std::ofstream cookieFile(params_.cookiePath, std::ios::out | std::ios::binary);
   const BinaryData ourIDKey = getOwnPubKey();
   if (ourIDKey.getSize() != BTC_ECKEY_COMPRESSED_LENGTH) {
      logger_->error("[TransportBIP15x::genBIPIDCookie] Client identity key ({})"
         " is uncompressed - will not write the identity cookie", params_.cookiePath);
      return false;
   }

   logger_->debug("[TransportBIP15x::genBIPIDCookie] writing a new client "
      "identity cookie ({}).", params_.cookiePath);
   cookieFile.write(getOwnPubKey().getCharPtr(), BTC_ECKEY_COMPRESSED_LENGTH);
   cookieFile.close();
   return true;
}

// Get the client's compressed BIP 150 identity public key.
//
// INPUT:  N/A
// OUTPUT: N/A
// RETURN: A buffer with the compressed ECDSA ID pub key. (BinaryData)
BinaryData TransportBIP15x::getOwnPubKey() const
{
   std::lock_guard<std::mutex> lock(authPeersMutex_);
   return getOwnPubKey(*authPeers_);
}

void TransportBIP15x::sendDisconnect()
{
   if (!bip151Connection_ || (bip151Connection_->getBIP150State() != BIP150State::SUCCESS)) {
      return;
   }
   logger_->debug("[TransportBIP15x::sendDisconnect]");

   auto packet = bip15x::MessageBuilder(bip15x::MsgType::Disconnect)
      .encryptIfNeeded(bip151Connection_.get()).build();
   // An error message is already logged elsewhere if the send fails.
   sendPacket(packet);
}
