/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef BASE_CELER_CLIENT_H
#define BASE_CELER_CLIENT_H

#include <memory>
#include <string>
#include <queue>
#include <functional>
#include <unordered_set>
#include <unordered_map>

#include <QObject>
#include <QTimer>

#include "MessageMapper.h"
#include "Property.h"
#include "CommonTypes.h"
#include "DataConnectionListener.h"
#include "IdStringGenerator.h"

class CommandSequence;
class DataConnection;
class CelerClientListener;

namespace spdlog {
   class logger;
}
namespace bs {
   namespace celer {
      class BaseCelerCommand;
   }
}

struct CelerCallbackTarget
{
   virtual void connectedToServer() {}
   virtual void connectionClosed() {}
   virtual void connectionError(int errorCode) {}
   virtual void onClosingConnection() {}

   virtual void setSendTimer(std::chrono::seconds) {}
   virtual void setRecvTimer(std::chrono::seconds) {}
   virtual void resetSendTimer() {}
   virtual void resetRecvTimer() {}
};

class BaseCelerClient
{
friend class CelerClientListener;

public:
   using CelerUserType = bs::network::UserType;

   enum CelerErrorCode
   {
      ResolveHostError,
      LoginError,
      ServerMaintainanceError,
      UndefinedError
   };

   using message_handler = std::function<bool (const std::string&)>;

public:
   BaseCelerClient(const std::shared_ptr<spdlog::logger> &logger, CelerCallbackTarget *
      , bool userIdRequired, bool useRecvTimer);
   virtual ~BaseCelerClient() noexcept = default;

   BaseCelerClient(const BaseCelerClient&) = delete;
   BaseCelerClient& operator = (const BaseCelerClient&) = delete;

   BaseCelerClient(BaseCelerClient&&) = delete;
   BaseCelerClient& operator = (BaseCelerClient&&) = delete;

   bool RegisterHandler(CelerAPI::CelerMessageType messageType, const message_handler& handler);

   bool ExecuteSequence(const std::shared_ptr<bs::celer::BaseCelerCommand>& command);

   bool IsConnected() const;

   // For CelerClient userName and email are the same.
   // For CelerClientProxy they are different!
   // Requests to Celer should always use userName, requests to PB and Genoa should use email.
   const std::string& userName() const { return userName_; }

   // Email will be always in lower case here
   const std::string& email() const { return email_; }

   const std::string& userId() const;
   const QString& userType() const { return userType_; }
   CelerUserType celerUserType() const { return celerUserType_; }

   bool tradingAllowed() const;

   std::unordered_set<std::string> GetSubmittedAuthAddressSet() const;
   bool SetSubmittedAuthAddressSet(const std::unordered_set<std::string>& addressSet);

   bool IsCCAddressSubmitted (const std::string &address) const;
   bool SetCCAddressSubmitted(const std::string &address);

   static void UpdateSetFromString(const std::string& value, std::unordered_set<std::string> &set);
   static std::string SetToString(const std::unordered_set<std::string> &set);

   // Call when there is need to send login request
   bool SendLogin(const std::string& login, const std::string& email, const std::string& password);

   // Call when there is new data was received
   void recvData(CelerAPI::CelerMessageType messageType, const std::string& data);

   virtual void CloseConnection();

protected:
   // Override to do actual data send
   virtual void onSendData(CelerAPI::CelerMessageType messageType, const std::string &data) = 0;

   virtual void onSendHbTimeout();
   virtual void onRecvHbTimeout();

private:
   void OnDataReceived(CelerAPI::CelerMessageType messageType, const std::string& data);
   void OnConnected();
   void OnDisconnected();
   void OnError(DataConnectionListener::DataConnectionError errorCode);

   void RegisterDefaulthandlers();

   bool sendMessage(CelerAPI::CelerMessageType messageType, const std::string& data);

   void AddInternalSequence(const std::shared_ptr<bs::celer::BaseCelerCommand>& commandSequence);

   void SendCommandMessagesIfRequired(const std::shared_ptr<bs::celer::BaseCelerCommand>& command);
   void RegisterUserCommand(const std::shared_ptr<bs::celer::BaseCelerCommand>& command);

   bool onHeartbeat(const std::string& message);
   bool onSingleMessage(const std::string& message);
   bool onExceptionResponse(const std::string& message);
   bool onMultiMessage(const std::string& message);

   bool SendDataToSequence(const std::string& sequenceId, CelerAPI::CelerMessageType messageType, const std::string& message);

   void loginSuccessCallback(const std::string& userName, const std::string& email, const std::string& sessionToken, std::chrono::seconds heartbeatInterval);
   void loginFailedCallback(const std::string& errorMessage);

   static void AddToSet(const std::string& address, std::unordered_set<std::string> &set);

protected:
   using commandsQueueType = std::queue<std::shared_ptr<bs::celer::BaseCelerCommand>>;
   commandsQueueType internalCommands_;

   std::unordered_map<CelerAPI::CelerMessageType, message_handler, std::hash<int>>  messageHandlersMap_;

   std::unordered_map<std::string, std::shared_ptr<bs::celer::BaseCelerCommand>>    activeCommands_;
   // Use recursive mutex here as active commands could probably call RegisterUserCommand again
   std::recursive_mutex activeCommandsMutex_;

   std::shared_ptr<spdlog::logger> logger_;
   CelerCallbackTarget* cct_{ nullptr };
   const bool useRecvTimer_;
   std::string sessionToken_;
   std::string userName_;
   std::string email_;
   QString userType_;
   CelerUserType celerUserType_;
   bs::celer::Property  userId_;
   bs::celer::Property bitcoinParticipant_;

   bs::celer::Property        submittedAuthAddressListProperty_;
   std::unordered_set<std::string> submittedAuthAddressSet_;

   bs::celer::Property        submittedCCAddressListProperty_;
   std::unordered_set<std::string> submittedCCAddressSet_;

   std::chrono::seconds    heartbeatInterval_{};

   IdStringGenerator       idGenerator_;
   bool                    userIdRequired_;

   bool serverNotAvailable_;
};


class CelerClientQt : public QObject, public BaseCelerClient, public CelerCallbackTarget
{
   Q_OBJECT
public:
   CelerClientQt(const std::shared_ptr<spdlog::logger>& logger, bool userIdRequired, bool useRecvTimer);
   ~CelerClientQt() noexcept override = default;

   CelerClientQt(const CelerClientQt&) = delete;
   CelerClientQt& operator = (const CelerClientQt&) = delete;
   CelerClientQt(CelerClientQt&&) = delete;
   CelerClientQt& operator = (CelerClientQt&&) = delete;

   void CloseConnection() override;

signals:
   void OnConnectedToServer();
   void OnConnectionClosed();
   void OnConnectionError(int errorCode);
   void closingConnection();

protected:
   // Override to do actual data send
   virtual void onSendData(CelerAPI::CelerMessageType messageType, const std::string& data) = 0;

private slots:
   void onSendHbTimeout() override { BaseCelerClient::onSendHbTimeout(); }
   void onRecvHbTimeout() override { BaseCelerClient::onRecvHbTimeout(); }

private:    // CelerCallbacks
   void connectedToServer() override { emit OnConnectedToServer(); }
   void connectionClosed() override { emit OnConnectionClosed(); }
   void connectionError(int errorCode) override { emit OnConnectionError(errorCode); }
   void onClosingConnection() override { emit closingConnection(); }

   void setSendTimer(std::chrono::seconds) override;
   void setRecvTimer(std::chrono::seconds) override;
   void resetSendTimer() override;
   void resetRecvTimer() override;

private:
   QTimer* timerSendHb_{};
   QTimer* timerRecvHb_{};
};

#endif
