/*

***********************************************************************************
* Copyright (C) 2019 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef CHATCLIENTSERVICE_H
#define CHATCLIENTSERVICE_H

#include <memory>

#include <QObject>

#include "ChatProtocol/ServiceThread.h"
#include "ChatProtocol/ChatClientLogic.h"
#include "BIP15xHelpers.h"

namespace Chat
{
   class ChatClientLogic;

   class ChatClientService : public ServiceThread<ChatClientLogic>
   {
      Q_OBJECT

   public:
      explicit ChatClientService(QObject* parent = nullptr);

      ClientPartyModelPtr getClientPartyModelPtr() const;

   signals:
      ////////// PROXY SIGNALS //////////
      void Init(const Chat::LoggerPtr& loggerPtr, Chat::ChatSettings);
      void LoginToServer(const BinaryData& token, const BinaryData& tokenSign
         , const bs::network::BIP15xNewKeyCb &);
      void LogoutFromServer();
      void SendPartyMessage(const std::string& partyId, const std::string& data);
      void RequestPrivateParty(const std::string& userName, const std::string& initialMessage = "");
      void RequestPrivatePartyOTC(const std::string& remoteUserName);
      void SetMessageSeen(const std::string& partyId, const std::string& messageId);
      void RejectPrivateParty(const std::string& partyId);
      void DeletePrivateParty(const std::string& partyId);
      void AcceptPrivateParty(const std::string& partyId);
      void SearchUser(const std::string& userHash, const std::string& searchId);
      void AcceptNewPublicKeys(const Chat::UserPublicKeyInfoList& userPublicKeyInfoList);
      void DeclineNewPublicKeys(const Chat::UserPublicKeyInfoList& userPublicKeyInfoList);
      void RequestPrivateMessagesHistoryCount(const std::string& partyId);
      void RequestAllHistoryMessages(const std::string& partyId);

      ////////// RETURN SIGNALS //////////
      void chatUserUserHashChanged(const std::string& chatUserUserHash);
      void chatClientError(const Chat::ChatClientLogicError& errorCode);
      void clientLoggedOutFromServer();
      void clientLoggedInToServer();
      void partyModelChanged();
      void initDone();
      void searchUserReply(const Chat::SearchUserReplyList& userHashList, const std::string& searchId);
      void privateMessagesHistoryCount(const std::string& partyId, quint64 count);
   };

   using ChatClientServicePtr = std::shared_ptr<ChatClientService>;

}

Q_DECLARE_METATYPE(Chat::LoggerPtr)

#endif // CHATCLIENTSERVICE_H
