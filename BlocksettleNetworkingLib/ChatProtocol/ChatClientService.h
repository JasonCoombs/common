#ifndef ChatClientService_h__
#define ChatClientService_h__

#include <memory>

#include <QObject>

#include "ChatProtocol/ServiceThread.h"
#include "ChatProtocol/ChatClientLogic.h"

namespace Chat
{
   class ChatClientService : public ServiceThread<ChatClientLogic>
   {
      Q_OBJECT

   public:
      explicit ChatClientService(QObject* parent = nullptr);

      ClientPartyModelPtr getClientPartyModelPtr();

   signals:
      ////////// PROXY SIGNALS //////////
      void Init(const Chat::ConnectionManagerPtr& connectionManagerPtr, const Chat::ApplicationSettingsPtr& appSettings, const Chat::LoggerPtr& loggerPtr);
      void LoginToServer(const std::string& email, const std::string& jwt, const ZmqBipNewKeyCb& cb);
      void LogoutFromServer();
      void SendPartyMessage(const std::string& partyId, const std::string& data);
      void RequestPrivateParty(const std::string& userName);
      void SetMessageSeen(const std::string& partyId, const std::string& messageId);
      void RejectPrivateParty(const std::string& partyId);
      void DeletePrivateParty(const std::string& partyId);
      void AcceptPrivateParty(const std::string& partyId);
      void SearchUser(const std::string& userHash, const std::string& searchId);

      ////////// RETURN SIGNALS //////////
      void chatUserUserNameChanged(const std::string& chatUserDisplayName);
      void chatClientError(const Chat::ChatClientLogicError& errorCode);
      void clientLoggedOutFromServer();
      void clientLoggedInToServer();
      void partyModelChanged();
      void initDone();
      void searchUserReply(const Chat::SearchUserReplyList& userHashList, const std::string& searchId);
   };

   using ChatClientServicePtr = std::shared_ptr<ChatClientService>;

}

Q_DECLARE_METATYPE(Chat::ConnectionManagerPtr)
Q_DECLARE_METATYPE(Chat::LoggerPtr)
Q_DECLARE_METATYPE(ZmqBipNewKeyCb)

#endif // ChatClientService_h__
