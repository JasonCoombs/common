#ifndef __REQ_XBT_SETTLEMENT_CONTAINER_H__
#define __REQ_XBT_SETTLEMENT_CONTAINER_H__

#include <memory>
#include <unordered_set>
#include "AddressVerificator.h"
#include "BSErrorCode.h"
#include "SettlementContainer.h"
#include "UtxoReservation.h"
#include "TransactionData.h"
#include "QWalletInfo.h"

namespace spdlog {
   class logger;
}
namespace bs {
   namespace sync {
      class WalletsManager;
   }
}
class AddressVerificator;
class ArmoryConnection;
class AuthAddressManager;
class SignContainer;
class QuoteProvider;
class TransactionData;


class ReqXBTSettlementContainer : public bs::SettlementContainer
{
   Q_OBJECT
public:
   ReqXBTSettlementContainer(const std::shared_ptr<spdlog::logger> &
      , const std::shared_ptr<AuthAddressManager> &
      , const std::shared_ptr<SignContainer> &
      , const std::shared_ptr<ArmoryConnection> &
      , const std::shared_ptr<bs::sync::WalletsManager> &
      , const bs::network::RFQ &
      , const bs::network::Quote &
      , const std::shared_ptr<TransactionData> &
      , const bs::Address &authAddr
   );
   ~ReqXBTSettlementContainer() override;

   bool cancel() override;

   void activate() override;
   void deactivate() override;

   std::string id() const override { return quote_.requestId; }
   bs::network::Asset::Type assetType() const override { return rfq_.assetType; }
   std::string security() const override { return rfq_.security; }
   std::string product() const override { return rfq_.product; }
   bs::network::Side::Type side() const override { return rfq_.side; }
   double quantity() const override { return quote_.quantity; }
   double price() const override { return quote_.price; }
   double amount() const override { return amount_; }
   bs::sync::PasswordDialogData toPasswordDialogData() const override;


   std::string fxProduct() const { return fxProd_; }
   uint64_t fee() const { return fee_; }
   bool weSell() const { return clientSells_; }
   bool userKeyOk() const { return userKeyOk_; }


   void onUnsignedPayinRequested(const std::string& settlementId);
   void onSignedPayoutRequested(const std::string& settlementId, const BinaryData& payinHash);
   void onSignedPayinRequested(const std::string& settlementId, const BinaryData& unsignedPayin);

signals:
   void settlementCancelled();
   void settlementAccepted();
   void acceptQuote(std::string reqId, std::string hexPayoutTx);
   void retry();

signals:
   void sendUnsignedPayinToPB(const std::string& settlementId, const BinaryData& unsignedPayin, const BinaryData& txId);
   void sendSignedPayinToPB(const std::string& settlementId, const BinaryData& signedPayin);
   void sendSignedPayoutToPB(const std::string& settlementId, const BinaryData& signedPayout);

private slots:
   void onTXSigned(unsigned int id, BinaryData signedTX, bs::error::ErrorCode, std::string error);
   void onTimerExpired();

private:
   unsigned int createPayoutTx(const BinaryData& payinHash, double qty, const bs::Address &recvAddr);

   void acceptSpotXBT();
   void dealerVerifStateChanged(AddressVerificationState);
   void activateProceed();

   void cancelWithError(const QString& errorMessage);

private:
   std::shared_ptr<spdlog::logger>           logger_;
   std::shared_ptr<AuthAddressManager>       authAddrMgr_;
   std::shared_ptr<bs::sync::WalletsManager> walletsMgr_;
   std::shared_ptr<SignContainer>            signContainer_;
   std::shared_ptr<ArmoryConnection>         armory_;
   std::shared_ptr<TransactionData>          transactionData_;

   bs::network::RFQ           rfq_;
   bs::network::Quote         quote_;
   bs::Address                settlAddr_;

   std::shared_ptr<AddressVerificator>             addrVerificator_;
   std::shared_ptr<bs::UtxoReservation::Adapter>   utxoAdapter_;

   double            amount_;
   std::string       fxProd_;
   uint64_t          fee_;
   BinaryData        settlementId_;
   std::string       settlementIdString_;
   BinaryData        userKey_;
   BinaryData        dealerAuthKey_;
   bs::Address       recvAddr_;
   AddressVerificationState dealerVerifState_ = AddressVerificationState::VerificationFailed;

   std::string       comment_;
   const bool        clientSells_;
   bool              userKeyOk_ = false;

   unsigned int      payinSignId_ = 0;
   unsigned int      payoutSignId_ = 0;

   const bs::Address authAddr_;
   bs::Address       dealerAuthAddress_;

   bs::core::wallet::TXSignRequest        unsignedPayinRequest_;
};

#endif // __REQ_XBT_SETTLEMENT_CONTAINER_H__
