/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __HEADLESS_CONTAINER_LISTENER_H__
#define __HEADLESS_CONTAINER_LISTENER_H__

#include <functional>
#include <memory>
#include <string>
#include <queue>
#include "BinaryData.h"
#include "CoreWallet.h"
#include "EncryptionUtils.h"
#include "ServerConnectionListener.h"
#include "SignerDefs.h"
#include "BSErrorCode.h"
#include "PasswordDialogData.h"
#include "PasswordDialogDataWrapper.h"


namespace spdlog {
   class logger;
}
namespace Blocksettle {
   namespace Communication {
      namespace Internal {
         class PasswordDialogDataWrapper;
      }
      namespace signer {
         enum PasswordDialogType : int;
      }
   }
}
namespace bs {
   namespace core {
      namespace hd {
         class Leaf;
         class Node;
         class Wallet;
      }
      class WalletsManager;
   }
   class Wallet;
}
class ServerConnection;
class DispatchQueue;

class HeadlessContainerCallbacks
{
public:
   virtual ~HeadlessContainerCallbacks() = default;

   virtual void clientConn(const std::string &, const ServerConnectionListener::Details &details) = 0;
   virtual void clientDisconn(const std::string &) = 0;

   virtual void decryptWalletRequest(Blocksettle::Communication::signer::PasswordDialogType dialogType
      , const Blocksettle::Communication::Internal::PasswordDialogDataWrapper &dialogData
      , const bs::core::wallet::TXSignRequest & = {}) = 0;

   virtual void txSigned(const BinaryData &) = 0;
   virtual void cancelTxSign(const BinaryData &txId) = 0;
   virtual void autoSignActivated(bool active, const std::string &walletId) = 0;
   virtual void updateDialogData(const Blocksettle::Communication::Internal::PasswordDialogDataWrapper &dialogData) = 0;
   virtual void xbtSpent(uint64_t amount, bool autoSign) = 0;
   virtual void customDialog(const std::string &, const std::string &) = 0;
   virtual void terminalHandshakeFailed(const std::string &peerAddress) = 0;

   virtual void walletChanged(const std::string &walletId) = 0;

   virtual void ccNamesReceived(bool) = 0;
};

using PasswordDialogFunc = std::function<void(const Blocksettle::Communication::Internal::PasswordDialogDataWrapper &)>;
using PasswordReceivedCb = std::function<void(bs::error::ErrorCode result, const SecureBinaryData &password)>;
using PasswordsReceivedCb = std::function<void(const std::unordered_map<std::string, SecureBinaryData> &)>;

struct PasswordRequest
{
   PasswordDialogFunc passwordRequest;
   PasswordReceivedCb callback;
   Blocksettle::Communication::Internal::PasswordDialogDataWrapper dialogData;
   std::chrono::steady_clock::time_point dialogRequestedTime{std::chrono::steady_clock::now()};
   std::chrono::steady_clock::time_point dialogExpirationTime{std::chrono::steady_clock::now()};

   // dialogs sorted by final time point in ascending order
   // first dialog in vector should be executed firstly
   bool operator < (const PasswordRequest &other) const;
};

class HeadlessContainerListener : public ServerConnectionListener
{
public:
   HeadlessContainerListener(const std::shared_ptr<spdlog::logger> &logger
      , const std::shared_ptr<bs::core::WalletsManager> &
      , const std::shared_ptr<DispatchQueue> &
      , const std::string &walletsPath
      , NetworkType netType
      , const bool &backupEnabled = true);
   ~HeadlessContainerListener() noexcept override;

   void SetLimits(const bs::signer::Limits &limits);

   bool disconnect(const std::string &clientId = {});

   void setCallbacks(HeadlessContainerCallbacks *callbacks);

   void passwordReceived(const std::string &walletId
      , bs::error::ErrorCode result, const SecureBinaryData &password);
   bs::error::ErrorCode activateAutoSign(const std::string &walletId, const SecureBinaryData &password);
   bs::error::ErrorCode deactivateAutoSign(const std::string &walletId = {}, bs::error::ErrorCode reason = bs::error::ErrorCode::NoError);
   void walletsListUpdated();
   void windowVisibilityChanged(bool visible);

   void resetConnection(ServerConnection *connection);

   // Used only to show prompt in terminal to create new wallets.
   // Terminal should not prompt if there is encrypted wallets with unknown master password.
   void setNoWallets(bool noWallets);

   // Used to sent command to listener to force update wallets
   // once they are ready
   void syncWallet();

protected:
   bool isAutoSignActive(const std::string &walletId) const;

   void onXbtSpent(const int64_t value, bool autoSign);

   void OnClientConnected(const std::string &clientId, const Details &details) override;
   void OnClientDisconnected(const std::string &clientId) override;
   void OnDataFromClient(const std::string &clientId, const std::string &data) override;
   void onClientError(const std::string &clientId, ClientError errorCode, const Details &details) override;

private:
   void passwordReceived(const std::string &clientId, const std::string &walletId
      , bs::error::ErrorCode result, const SecureBinaryData &password);

   bool sendData(const std::string &data, const std::string &clientId = {});
   bool onRequestPacket(const std::string &clientId, Blocksettle::Communication::headless::RequestPacket packet);

   bool onSignTxRequest(const std::string &clientId, const Blocksettle::Communication::headless::RequestPacket &packet
      , Blocksettle::Communication::headless::RequestType requestType);
   //bool onSignSettlementPayoutTxRequest(const std::string &clientId
   //   , const Blocksettle::Communication::headless::RequestPacket &packet);
   //bool onSignAuthAddrRevokeRequest(const std::string &clientId, const Blocksettle::Communication::headless::RequestPacket &);
   bool onResolvePubSpenders(const std::string &clientId, const Blocksettle::Communication::headless::RequestPacket &packet);
   bool onCreateHDLeaf(const std::string &clientId, Blocksettle::Communication::headless::RequestPacket &packet);
   //bool onEnableTradingInWallet(const std::string& clientId, Blocksettle::Communication::headless::RequestPacket& packet);
   //bool onPromoteWalletToPrimary(const std::string& clientId, Blocksettle::Communication::headless::RequestPacket& packet);
   //bool onSetUserId(const std::string &clientId, Blocksettle::Communication::headless::RequestPacket &packet);
   //bool onSyncCCNames(Blocksettle::Communication::headless::RequestPacket &packet);
   bool onGetHDWalletInfo(const std::string &clientId, Blocksettle::Communication::headless::RequestPacket &packet);
   bool onCancelSignTx(const std::string &clientId, Blocksettle::Communication::headless::RequestPacket packet);
   bool onUpdateDialogData(const std::string &clientId, Blocksettle::Communication::headless::RequestPacket packet);
   bool onSyncWalletInfo(const std::string &clientId, Blocksettle::Communication::headless::RequestPacket packet);
   bool onSyncHDWallet(const std::string &clientId, Blocksettle::Communication::headless::RequestPacket packet);
   bool onSyncWallet(const std::string &clientId, Blocksettle::Communication::headless::RequestPacket packet);
   bool onSyncComment(const std::string &clientId, Blocksettle::Communication::headless::RequestPacket packet);
   bool onSyncAddresses(const std::string &clientId, Blocksettle::Communication::headless::RequestPacket packet);
   bool onExtAddrChain(const std::string &clientId, Blocksettle::Communication::headless::RequestPacket packet);
   bool onSyncNewAddr(const std::string &clientId, Blocksettle::Communication::headless::RequestPacket packet);
   //bool onChatNodeRequest(const std::string &clientId, Blocksettle::Communication::headless::RequestPacket packet);
   //bool onSettlAuthRequest(const std::string &clientId, Blocksettle::Communication::headless::RequestPacket packet);
   //bool onSettlCPRequest(const std::string &clientId, Blocksettle::Communication::headless::RequestPacket packet);
   bool onExecCustomDialog(const std::string &clientId, Blocksettle::Communication::headless::RequestPacket packet);

   //bool onCreateSettlWallet(const std::string &clientId, Blocksettle::Communication::headless::RequestPacket packet);
   //bool onSetSettlementId(const std::string &clientId, Blocksettle::Communication::headless::RequestPacket packet);
   //bool onGetPayinAddr(const std::string &clientId, Blocksettle::Communication::headless::RequestPacket packet);
   //bool onSettlGetRootPubkey(const std::string &clientId, Blocksettle::Communication::headless::RequestPacket packet);

   bool AuthResponse(const std::string &clientId, Blocksettle::Communication::headless::RequestPacket packet);
   void SignTXResponse(const std::string &clientId, unsigned int id, Blocksettle::Communication::headless::RequestType reqType
      , bs::error::ErrorCode errorCode, const BinaryData &tx = {});
   void CreateHDLeafResponse(const std::string &clientId, unsigned int id, bs::error::ErrorCode result
      , const std::shared_ptr<bs::core::hd::Leaf>& leaf = nullptr);
   void CreateEnableTradingResponse(const std::string& clientId, unsigned int id, bs::error::ErrorCode result,
                                const std::string& walletId);
   void CreatePromoteWalletResponse(const std::string& clientId, unsigned int id, bs::error::ErrorCode result,
                                const std::string& walletId);
   void GetHDWalletInfoResponse(const std::string &clientId, unsigned int id, const std::string &walletId
      , const std::shared_ptr<bs::core::hd::Wallet> &, const std::string &error = {});
   void SyncAddrsResponse(const std::string &clientId, unsigned int id, const std::string &walletId, bs::sync::SyncState);
   //void setUserIdResponse(const std::string &clientId, unsigned int id
   //   , Blocksettle::Communication::headless::AuthWalletResponseType, const std::string &walletId = {});
   void AutoSignActivatedEvent(bs::error::ErrorCode result, const std::string &walletId);

   bool RequestPasswordIfNeeded(const std::string &clientId, const std::string &walletId
      , const bs::core::wallet::TXSignRequest &
      , Blocksettle::Communication::headless::RequestType reqType, const Blocksettle::Communication::Internal::PasswordDialogDataWrapper &dialogData
      , const PasswordReceivedCb &cb);
   bool RequestPasswordsIfNeeded(int reqId, const std::string &clientId
      , const bs::core::wallet::TXMultiSignRequest &, const bs::core::WalletMap &
      , const PasswordsReceivedCb &cb);
   bool RequestPassword(const std::string &rootId, const bs::core::wallet::TXSignRequest &
      , Blocksettle::Communication::headless::RequestType reqType, const Blocksettle::Communication::Internal::PasswordDialogDataWrapper &dialogData
      , const PasswordReceivedCb &cb);
   void RunDeferredPwDialog();

   //bool createAuthLeaf(const std::shared_ptr<bs::core::hd::Wallet> &, const BinaryData &salt);
   //bool createSettlementLeaves(const std::shared_ptr<bs::core::hd::Wallet> &wallet
   //   , const std::vector<bs::Address> &authAddresses);

   bool checkSpendLimit(uint64_t value, const std::string &walletId, bool autoSign);

   void sendUpdateStatuses(std::string clientId = {});

   void sendSyncWallets(std::string clientId = {});

private:
   std::shared_ptr<spdlog::logger>     logger_;
   ServerConnection                    *connection_{};
   std::shared_ptr<bs::core::WalletsManager> walletsMgr_;
   std::shared_ptr<DispatchQueue>      queue_;
   const std::string                   walletsPath_;
   const std::string                   backupPath_;
   const NetworkType                   netType_;
   bs::signer::Limits                  limits_;
   std::unordered_map<std::string, ServerConnectionListener::Details>     connectedClients_;

   std::unordered_map<std::string, SecureBinaryData>                 passwords_;
   //std::unordered_set<std::string>  autoSignPwdReqs_;

   std::vector<PasswordRequest> deferredPasswordRequests_;
   bool deferredDialogRunning_ = false;

   struct TempPasswords {
      std::unordered_map<std::string, std::unordered_set<std::string>>  rootLeaves;
      std::unordered_set<std::string>  reqWalletIds;
      std::unordered_map<std::string, SecureBinaryData> passwords;
   };
   std::unordered_map<int, TempPasswords> tempPasswords_;

   const bool backupEnabled_ = true;

   HeadlessContainerCallbacks *callbacks_{};

   std::map<std::pair<std::string, bs::Address>, std::vector<uint32_t>> settlLeafReqs_;

   bool noWallets_{false};

};

#endif // __HEADLESS_CONTAINER_LISTENER_H__
