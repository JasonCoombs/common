/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __AUTH_EID_CLIENT_H__
#define __AUTH_EID_CLIENT_H__

#include <functional>
#include <QObject>
#include <QNetworkReply>
#include "EncryptionUtils.h"
#include "autheid_utils.h"

namespace spdlog {
   class logger;
}

namespace autheid {
   namespace rp {
      class GetResultResponse_SignatureResult;
   }
}

class QNetworkReply;
class QNetworkAccessManager;

enum class AuthEidEnv : int
{
   Prod,
   Test,
   Staging,
};

class AutheIDClient : public QObject
{
   Q_OBJECT

public:
   using AuthKeys = std::pair<autheid::PrivateKey, autheid::PublicKey>;

   // Keep in sync with autheid::rp::Serialization
   enum class Serialization
   {
      Json,
      Protobuf,
   };

   struct DeviceInfo
   {
      std::string userId;
      std::string deviceId;
      std::string deviceName;
   };

   struct SignRequest
   {
      std::string title;
      std::string description;
      std::string email;
      Serialization serialization{Serialization::Protobuf};
      BinaryData invisibleData;
      int expiration{AutheIDClient::kDefaultSettlementExpiration};
   };

   struct SignResult
   {
      Serialization serialization{};
      BinaryData data;
      BinaryData sign;
      BinaryData certificateClient;
      BinaryData certificateIssuer;
      BinaryData ocspResponse;
   };

   enum RequestType
   {
      Unknown,
      ActivateWallet,
      DeactivateWallet,
      SignWallet,
      BackupWallet,
      ActivateWalletOldDevice,
      ActivateWalletNewDevice,
      DeactivateWalletDevice,
      VerifyWalletKey,
      ActivateOTP,
      CreateAuthLeaf,
      CreateSettlementLeaf,
      EnableTrading,
      PromoteWallet,
      EnableAutoSign,
      RevokeAuthAddress,
      SubmitEquityToken,
      // Private market and others with lower timeout
      SettlementTransaction,

      // Please also add new type text in getAutheIDClientRequestText
   };
   Q_ENUM(RequestType)

   enum ErrorType
   {
      NoError,
      CreateError,
      DecodeError,
      DecryptError,
      InvalidSecureReplyError,
      InvalidKeySizeError,
      MissingSignatuteError,
      SerializationSignatureError,
      ParseSignatureError,
      Timeout,
      Cancelled,
      NotAuthenticated,
      ServerError,
      NetworkError,
      NoNewDeviceAvailable,
      WrongAccountForDeviceAdding,
   };
   Q_ENUM(ErrorType)

   struct SignVerifyStatus
   {
      bool valid{false};
      std::string errorMsg;

      // From client's certificate common name
      std::string uniqueUserId;

      // Data that was signed by client
      std::string email;
      std::string rpName;
      std::string title;
      std::string description;
      std::chrono::system_clock::time_point finished{};
      BinaryData invisibleData;

      static SignVerifyStatus failed(const std::string &errorMsg)
      {
         SignVerifyStatus result;
         result.errorMsg = errorMsg;
         return result;
      }
   };

   static QString errorString(ErrorType error);

   static DeviceInfo getDeviceInfo(const std::string &encKey);
   static constexpr int kDefaultExpiration = 120;
   static constexpr int kDefaultSettlementExpiration = 30;

   // Verifies signature only
   // Check uniqueUserId to make sure that valid user did sign request.
   // Check invisibleData and other fields to make sure that valid request was signed.
   // OCSP must be valid at the moment when request was signed (`finished` timepoint).
   static SignVerifyStatus verifySignature(const SignResult &result, AuthEidEnv env);

   // QNetworkAccessManager must live long enough to be able send cancel message
   // (if cancelling request in mobile app is needed)
   AutheIDClient(const std::shared_ptr<spdlog::logger> &, const std::shared_ptr<QNetworkAccessManager> &
      , const AuthKeys &authKeys, AuthEidEnv authEidEnv, QObject *parent = nullptr);
   ~AutheIDClient() override;

   // If timestamp is set (unix time in seconds) then auth eid server will use correct timeout.
   // timestamp must be valid value!
   // if email is empty then local request (QR code) will be used.
   void getDeviceKey(RequestType requestType, const std::string &email, const std::string &walletId, const QString &authEidMessage
      , const std::vector<std::string> &knownDeviceIds, const std::string &qrSecret = "", int expiration = kDefaultExpiration, int timestamp = 0
      , const std::string &oldEmail = {});

   void sign(const SignRequest &request, bool autoRequestResult = true);

   void authenticate(const std::string &email, int expiration = kDefaultExpiration, bool autoRequestResult = true);

   void cancel();

   void requestResult();

   void setApiKey(const std::string &apiKey);

signals:
   void createRequestDone();
   void requestIdReceived(const std::string &requestId);
   void succeeded(const std::string& encKey, const SecureBinaryData &password);
   void signSuccess(const SignResult &result);
   void authSuccess(const std::string &jwt);
   void failed(ErrorType error);
   void userCancelled();

private:
   struct Result
   {
      QByteArray payload;
      ErrorType authError;
      QNetworkReply::NetworkError networkError;
      //std::string errorMsg;
   };

   using ResultCallback = std::function<void(const Result &result)>;

   void createCreateRequest(const std::string &payload, int expiration, bool autoRequestResult);
   void processCreateReply(const QByteArray &payload, int expiration, bool autoRequestResult);
   void processResultReply(const QByteArray &payload);

   void processNetworkReply(QNetworkReply *, int timeoutSeconds, const ResultCallback &);

   void processSignatureReply(const autheid::rp::GetResultResponse_SignatureResult &);

   QString getAutheIDClientRequestText(RequestType requestType);
   bool isAutheIDClientNewDeviceNeeded(RequestType requestType);

   std::string finalMessageChange(const QString &authEidMessage, RequestType requestType,
      const std::vector<std::string> &knownDeviceIds);

private:
   std::shared_ptr<spdlog::logger> logger_;
   std::shared_ptr<QNetworkAccessManager> nam_;
   std::string requestId_;
   int expiration_{};
   std::string email_;
   std::string oldEmail_;
   std::string qrSecret_;
   const AuthKeys authKeys_;
   bool resultAuth_{};

   std::vector<std::string> knownDeviceIds_;

   SignRequest signRequest_;

   const char *baseUrl_;
   std::string apiKey_;
   RequestType requestType_{};
};

Q_DECLARE_METATYPE(AutheIDClient::RequestType)

#endif // __AUTH_EID_CLIENT_H__
