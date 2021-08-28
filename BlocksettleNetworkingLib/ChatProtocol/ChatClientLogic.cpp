/*

***********************************************************************************
* Copyright (C) 2019 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include <QRegularExpression>
#include <google/protobuf/any.pb.h>

#include "ChatProtocol/ChatClientLogic.h"

#include "Bip15xDataConnection.h"
#include "ConnectionManager.h"
#include "DataConnection.h"
#include "ProtobufUtils.h"
#include "WsDataConnection.h"

#include <disable_warnings.h>
#include <spdlog/spdlog.h>
#include <enable_warnings.h>

#include "bs_types.pb.h"
#include "chat.pb.h"

using namespace Chat;

const auto kEmailRegex = QStringLiteral(R"(^\S+@\S+\.\S+$)");

ChatClientLogic::ChatClientLogic()
{
   qRegisterMetaType<DataConnectionListener::DataConnectionError>();
   qRegisterMetaType<Chat::ChatClientLogicError>();
   qRegisterMetaType<Chat::ClientPartyLogicPtr>();
   qRegisterMetaType<Chat::ChatUserPtr>();
   qRegisterMetaType<bs::network::BIP15xNewKeyCb>();

   connect(this, &ChatClientLogic::chatClientError, this, &ChatClientLogic::handleLocalErrors);
}

ChatClientLogic::~ChatClientLogic() = default;

void ChatClientLogic::initDbDone()
{
   connect(currentUserPtr_.get(), &ChatUser::userHashChanged, this, &ChatClientLogic::chatUserUserHashChanged);

   setClientPartyLogicPtr(std::make_shared<ClientPartyLogic>(loggerPtr_, clientDBServicePtr_, this));
   connect(clientPartyLogicPtr_.get(), &ClientPartyLogic::partyModelChanged, this, &ChatClientLogic::partyModelChanged);
   connect(clientPartyLogicPtr_.get(), &ClientPartyLogic::privatePartyCreated, this, &ChatClientLogic::privatePartyCreated);
   connect(clientPartyLogicPtr_.get(), &ClientPartyLogic::privatePartyAlreadyExist, this, &ChatClientLogic::privatePartyAlreadyExist);
   connect(clientPartyLogicPtr_.get(), &ClientPartyLogic::deletePrivateParty, this, &ChatClientLogic::DeletePrivateParty);
   connect(this, &ChatClientLogic::clientLoggedOutFromServer, clientPartyLogicPtr_.get(), &ClientPartyLogic::loggedOutFromServer);
   connect(clientPartyLogicPtr_.get(), &ClientPartyLogic::acceptOTCPrivateParty, this, &ChatClientLogic::AcceptPrivateParty);

   sessionKeyHolderPtr_ = std::make_shared<SessionKeyHolder>(loggerPtr_, this);
   clientConnectionLogicPtr_ = std::make_shared<ClientConnectionLogic>(
      clientPartyLogicPtr_,
      clientDBServicePtr_,
      loggerPtr_,
      cryptManagerPtr_,
      sessionKeyHolderPtr_,
      this);
   clientConnectionLogicPtr_->setCurrentUserPtr(currentUserPtr_);
   connect(this, &ChatClientLogic::dataReceived, clientConnectionLogicPtr_.get(), &ClientConnectionLogic::onDataReceived);
   connect(this, &ChatClientLogic::connected, clientConnectionLogicPtr_.get(), &ClientConnectionLogic::onConnected);
   connect(this, &ChatClientLogic::disconnected, clientConnectionLogicPtr_.get(), &ClientConnectionLogic::onDisconnected);
   connect(this, qOverload<DataConnectionListener::DataConnectionError>(&ChatClientLogic::error),
      clientConnectionLogicPtr_.get(), qOverload<DataConnectionListener::DataConnectionError>(&ClientConnectionLogic::onError));
   connect(this, &ChatClientLogic::messagePacketSent, clientConnectionLogicPtr_.get(), &ClientConnectionLogic::messagePacketSent);
   connect(clientConnectionLogicPtr_.get(), &ClientConnectionLogic::sendPacket, this, &ChatClientLogic::sendPacket);
   connect(clientConnectionLogicPtr_.get(), &ClientConnectionLogic::closeConnection, this, &ChatClientLogic::onCloseConnection);
   connect(clientConnectionLogicPtr_.get(), &ClientConnectionLogic::searchUserReply, this, &ChatClientLogic::searchUserReply);
   connect(clientConnectionLogicPtr_.get(), &ClientConnectionLogic::properlyConnected, this, &ChatClientLogic::properlyConnected);
   connect(clientConnectionLogicPtr_.get(), &ClientConnectionLogic::deletePrivateParty, this, &ChatClientLogic::DeletePrivateParty);
   connect(clientDBServicePtr_.get(), &ClientDBService::privateMessagesHistoryCount, this, &ChatClientLogic::privateMessagesHistoryCount);

   // close connection from callback
   connect(this, &ChatClientLogic::disconnected, this, &ChatClientLogic::onCloseConnection);

   // OTC
   connect(this, &ChatClientLogic::otcPrivatePartyReady, clientPartyLogicPtr_->clientPartyModelPtr().get(), &ClientPartyModel::otcPrivatePartyReady);

   emit initDone();
}

void ChatClientLogic::Init(Chat::LoggerPtr loggerPtr, ChatSettings chatSettings)
{
   if (chatSettings_.connectionManager) {
      // already initialized
      emit chatClientError(ChatClientLogicError::ConnectionAlreadyInitialized);
      return;
   }

   loggerPtr_ = loggerPtr;

   cryptManagerPtr_ = std::make_shared<CryptManager>(loggerPtr);

   clientDBServicePtr_ = std::make_shared<ClientDBService>();
   connect(clientDBServicePtr_.get(), &ClientDBService::initDone, this, &ChatClientLogic::initDbDone);

   currentUserPtr_ = std::make_shared<ChatUser>();
   currentUserPtr_->setPrivateKey(chatSettings.chatPrivKey);
   currentUserPtr_->setPublicKey(chatSettings.chatPubKey);

   clientDBServicePtr_->Init(loggerPtr, chatSettings.chatDbFile, currentUserPtr_, cryptManagerPtr_);

   chatSettings_ = std::move(chatSettings);
}

void ChatClientLogic::LoginToServer(const BinaryData &token, const BinaryData &tokenSign
   , const bs::network::BIP15xNewKeyCb &cb)
{
   bs::types::ChatToken chatToken;
   const auto result = chatToken.ParseFromArray(token.getPtr(), static_cast<int>(token.getSize()));
   if (!result) {
      SPDLOG_LOGGER_ERROR(loggerPtr_, "parsing ChatToken failed");
      return;
   }

   if (connectionPtr_) {
      loggerPtr_->error("[ChatClientLogic::LoginToServer] connecting with not purged connection");

      emit chatClientError(ChatClientLogicError::ConnectionAlreadyUsed);
      connectionPtr_->closeConnection();
      connectionPtr_.reset();
   }

   bs::network::BIP15xParams params;
   params.ephemeralPeers = true;
   params.authMode = bs::network::BIP15xAuthMode::OneWay;

   const auto &transport = std::make_shared<bs::network::TransportBIP15xClient>(loggerPtr_, params);
   transport->setKeyCb(cb);
   auto wsConn = std::make_unique<WsDataConnection>(loggerPtr_, WsDataConnectionParams{});
   auto conn = std::make_unique<Bip15xDataConnection>(loggerPtr_, std::move(wsConn), transport);
   connectionPtr_ = std::move(conn);

   clientConnectionLogicPtr_->setToken(token, tokenSign);

   currentUserPtr_->setUserHash(chatToken.chat_login());
   currentUserPtr_->setCelerUserType(static_cast<bs::network::UserType>(chatToken.user_type()));
   clientPartyModelPtr()->setOwnUserName(currentUserPtr_->userHash());
   //TODO: replace clientPartyModelPtr()->setOwnCelerUserType(currentUserPtr_->celerUserType());

   if (!connectionPtr_->openConnection(this->getChatServerHost(), this->getChatServerPort(), this)) {
      loggerPtr_->error("[ChatClientLogic::LoginToServer] failed to open ZMQ data connection");
      connectionPtr_.reset();
      clientPartyModelPtr()->setOwnUserName({});
      //TODO: replace clientPartyModelPtr()->setOwnCelerUserType(bs::network::UserType::Undefined);

      emit chatClientError(ChatClientLogicError::ZmqDataConnectionFailed);
      emit clientLoggedOutFromServer();
   }
}

std::string ChatClientLogic::getChatServerHost() const
{
   return chatSettings_.chatServerHost;
}

std::string ChatClientLogic::getChatServerPort() const
{
   return chatSettings_.chatServerPort;
}

void ChatClientLogic::OnDataReceived(const std::string& data)
{
   emit dataReceived(data);
}

void ChatClientLogic::OnConnected()
{
   emit connected();
}

void ChatClientLogic::OnDisconnected()
{
   emit disconnected();
}

void ChatClientLogic::OnError(DataConnectionListener::DataConnectionError dataConnectionError)
{
   const QString errorString = QStringLiteral("DataConnectionError: %1").arg(dataConnectionError);
   emit chatClientError(ChatClientLogicError::ZmqDataConnectionFailed, errorString.toStdString());
   emit error(dataConnectionError);
   OnDisconnected();
}

void ChatClientLogic::sendPacket(const google::protobuf::Message& message)
{
   const auto packetString = ProtobufUtils::pbMessageToString(message);

   google::protobuf::Any any;
   any.ParseFromString(packetString);

   loggerPtr_->debug("[ChatClientLogic::sendPacket] send: {}", ProtobufUtils::toJsonCompact(any));

   if (!connectionPtr_->isActive()) {
      loggerPtr_->error("[ChatClientLogic::sendPacket] Connection is not alive!");
      return;
   }

   if (!connectionPtr_->send(packetString)) {
      loggerPtr_->error("[ChatClientLogic::sendPacket] Failed to send packet!");
      return;
   }

   if (any.Is<PartyMessagePacket>()) {
      // update message state to SENT value
      PartyMessagePacket partyMessagePacket;
      any.UnpackTo(&partyMessagePacket);

      emit messagePacketSent(partyMessagePacket.message_id());
   }
}

void ChatClientLogic::LogoutFromServer()
{
   if (!connectionPtr_) {
      emit clientLoggedOutFromServer();
      return;
   }

   const LogoutRequest logoutRequest;
   sendPacket(logoutRequest);
}

void ChatClientLogic::onCloseConnection()
{
   if (nullptr == connectionPtr_) {
      return;
   }
   connectionPtr_.reset();
   emit clientLoggedOutFromServer();
}

void ChatClientLogic::SendPartyMessage(const std::string& partyId, const std::string& data)
{
   const auto clientPartyPtr = clientPartyLogicPtr_->clientPartyModelPtr()->getClientPartyById(partyId);

   if (nullptr == clientPartyPtr) {
      emit chatClientError(ChatClientLogicError::ClientPartyNotExist, partyId);
      return;
   }

   clientConnectionLogicPtr_->prepareAndSendMessage(clientPartyPtr, data);
}

void ChatClientLogic::handleLocalErrors(const ChatClientLogicError& errorCode, const std::string& what) const
{
   loggerPtr_->debug("[ChatClientLogic::handleLocalErrors] Error: {}, what: {}", int(errorCode), what);
}

void ChatClientLogic::SetMessageSeen(const std::string& partyId, const std::string& messageId)
{
   const auto clientPartyPtr = clientPartyLogicPtr_->clientPartyModelPtr()->getClientPartyById(partyId);

   if (nullptr == clientPartyPtr) {
      emit chatClientError(ChatClientLogicError::ClientPartyNotExist, partyId);
      return;
   }

   clientConnectionLogicPtr_->setMessageSeen(clientPartyPtr, messageId);
}

void ChatClientLogic::RequestPrivatePartyOTC(const std::string& remoteUserName) const
{
   clientPartyLogicPtr_->createPrivateParty(currentUserPtr_, remoteUserName, OTC);
}

void ChatClientLogic::RequestPrivateParty(const std::string& remoteUserName, const std::string& initialMessage) const
{
   clientPartyLogicPtr_->createPrivateParty(currentUserPtr_, remoteUserName, STANDARD, initialMessage);
}

void ChatClientLogic::privatePartyCreated(const std::string& partyId) const
{
   clientConnectionLogicPtr_->prepareRequestPrivateParty(partyId);
}

void ChatClientLogic::privatePartyAlreadyExist(const std::string& partyId) const
{
   // check if it's otc private party
   const auto clientPartyPtr = clientPartyModelPtr()->getClientPartyById(partyId);

   clientConnectionLogicPtr_->prepareRequestPrivateParty(partyId);
}

void ChatClientLogic::RejectPrivateParty(const std::string& partyId) const
{
   clientConnectionLogicPtr_->rejectPrivateParty(partyId);
}

void ChatClientLogic::AcceptPrivateParty(const std::string& partyId) const
{
   clientConnectionLogicPtr_->acceptPrivateParty(partyId);
}

void ChatClientLogic::DeletePrivateParty(const std::string& partyId)
{
   // set party state as rejected
   clientConnectionLogicPtr_->rejectPrivateParty(partyId);

   // then delete local
   auto clientPartyModelPtr = clientPartyLogicPtr_->clientPartyModelPtr();
   const auto partyPtr = clientPartyModelPtr->getPartyById(partyId);

   if (nullptr == partyPtr) {
      emit chatClientError(ChatClientLogicError::PartyNotExist, partyId);
      return;
   }

   // TODO: remove party and all messages

   // if party in rejected state then remove recipients public keys, we don't need them anymore
   auto clientPartyPtr = clientPartyModelPtr->getClientPartyById(partyId);
   if (clientPartyPtr && clientPartyPtr->isPrivate()) {
      const auto recipients = clientPartyPtr->getRecipientsExceptMe(currentUserPtr_->userHash());
      clientDBServicePtr_->deleteRecipientsKeys(recipients);
   }
   clientPartyModelPtr->removeParty(partyPtr);
}

void ChatClientLogic::SearchUser(const std::string& userHash, const std::string& searchId) const
{
   clientConnectionLogicPtr_->searchUser(userHash, searchId);
}

void ChatClientLogic::AcceptNewPublicKeys(const Chat::UserPublicKeyInfoList& userPublicKeyInfoList) const
{
   PartyRecipientsPtrList recipientsToUpdate;
   std::vector<std::string> partiesToCheckUnsentMessages;

   for (const auto& userPkPtr : userPublicKeyInfoList) {
      // update loaded user key
      const auto userHash = userPkPtr->user_hash().toStdString();
      // update keys only for existing private parties
      auto clientPartyPtrList = clientPartyModelPtr()->getStandardPrivatePartyListForRecipient(userHash);
      for (const auto& clientPartyPtr : clientPartyPtrList) {
         auto existingRecipient = clientPartyPtr->getRecipient(userHash);

         if (existingRecipient) {
            existingRecipient->setPublicKey(userPkPtr->newPublicKey());
            existingRecipient->setPublicKeyTime(userPkPtr->newPublicKeyTime());

            recipientsToUpdate.push_back(existingRecipient);

            // force clear session keys
            sessionKeyHolderPtr_->clearSessionForUser(existingRecipient->userHash());

            // save party id for handling later
            partiesToCheckUnsentMessages.push_back(clientPartyPtr->id());
         }
      }
   }

   clientDBServicePtr_->updateRecipientKeys(recipientsToUpdate);

   // after updating the keys, check if we have unsent messages
   for (const auto& partyId : partiesToCheckUnsentMessages) {
      clientDBServicePtr_->checkUnsentMessages(partyId);
   }

   clientPartyLogicPtr_->updateModelAndRefreshPartyDisplayNames();
}

void ChatClientLogic::DeclineNewPublicKeys(const UserPublicKeyInfoList& userPublicKeyInfoList)
{
   // remove all parties for declined user
   for (const auto& userPkPtr : userPublicKeyInfoList) {
      const std::string userHash = userPkPtr->user_hash().toStdString();
      auto clientPartyPtrList = clientPartyModelPtr()->getStandardPrivatePartyListForRecipient(userHash);

      for (const auto& clientPartyPtr : clientPartyPtrList)
      {
         DeletePrivateParty(clientPartyPtr->id());
      }
   }

   clientPartyLogicPtr_->updateModelAndRefreshPartyDisplayNames();
}

void ChatClientLogic::RequestPrivateMessagesHistoryCount(const std::string& partyId) const
{
   clientDBServicePtr_->requestPrivateMessagesHistoryCount(partyId, currentUserPtr_->userHash());
}

void ChatClientLogic::RequestAllHistoryMessages(const std::string& partyId) const
{
   clientDBServicePtr_->requestAllHistoryMessages(partyId, currentUserPtr_->userHash());
}
