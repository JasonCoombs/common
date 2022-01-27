﻿/*

***********************************************************************************
* Copyright (C) 2018 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "HeadlessContainerListener.h"

#include "CheckRecipSigner.h"
#include "ConnectionManager.h"
#include "CoreHDWallet.h"
#include "CoreWalletsManager.h"
#include "DispatchQueue.h"
#include "ProtobufHeadlessUtils.h"
#include "ServerConnection.h"
#include "StringUtils.h"
#include "WalletEncryption.h"
#include "ZmqHelperFunctions.h"

#include <spdlog/spdlog.h>

#include "headless.pb.h"
#include "bs_signer.pb.h"
#include "Blocksettle_Communication_Internal.pb.h"

using namespace Blocksettle::Communication;
using namespace bs::error;
using namespace bs::sync;
using namespace std::chrono;

constexpr std::chrono::seconds kDefaultDuration{120};

namespace {
   bool invalidPasswordError(const std::exception &e)
   {
      return std::strcmp(e.what(), "witness data missing signature") == 0 ||
             std::strcmp(e.what(), "signer failed to verify") == 0;
   }
}

HeadlessContainerListener::HeadlessContainerListener(const std::shared_ptr<spdlog::logger> &logger
   , const std::shared_ptr<bs::core::WalletsManager> &walletsMgr
   , const std::shared_ptr<DispatchQueue> &queue
   , const std::string &walletsPath, NetworkType netType
   , const bool &backupEnabled)
   : ServerConnectionListener()
   , logger_(logger)
   , walletsMgr_(walletsMgr)
   , queue_(queue)
   , walletsPath_(walletsPath)
   , backupPath_(walletsPath + "/../backup")
   , netType_(netType)
   , backupEnabled_(backupEnabled)
{}

void HeadlessContainerListener::setCallbacks(HeadlessContainerCallbacks *callbacks)
{
   callbacks_ = callbacks;
}

HeadlessContainerListener::~HeadlessContainerListener() noexcept
{
   disconnect();
}

bool HeadlessContainerListener::disconnect(const std::string &clientId)
{
   headless::RequestPacket packet;
   packet.set_data("");
   packet.set_type(headless::DisconnectionRequestType);
   const auto &serializedPkt = packet.SerializeAsString();

   bool rc = sendData(serializedPkt, clientId);
   if (rc && !clientId.empty()) {
      OnClientDisconnected(clientId);
   }
   return rc;
}

bool HeadlessContainerListener::sendData(const std::string &data, const std::string &clientId)
{
   if (!connection_) {
      return false;
   }

   bool sentOk = false;
   if (clientId.empty()) {
      for (const auto &clientId : connectedClients_) {
         if (connection_->SendDataToClient(clientId.first, data)) {
            sentOk = true;
         }
      }
   }
   else {
      sentOk = connection_->SendDataToClient(clientId, data);
   }
   return sentOk;
}

void HeadlessContainerListener::SetLimits(const bs::signer::Limits &limits)
{
   limits_ = limits;
}

void HeadlessContainerListener::OnClientConnected(const std::string &clientId, const Details &details)
{
   logger_->debug("[HeadlessContainerListener] client {} connected", bs::toHex(clientId));

   queue_->dispatch([this, clientId, details] {
      connectedClients_.insert(std::make_pair(clientId, details));
      sendUpdateStatuses(clientId);
      if (callbacks_) {
         callbacks_->clientConn(clientId, details);
      }
   });
}

void HeadlessContainerListener::OnClientDisconnected(const std::string &clientId)
{
   logger_->debug("[HeadlessContainerListener] client {} disconnected", bs::toHex(clientId));

   queue_->dispatch([this, clientId] {
      connectedClients_.erase(clientId);
      if (callbacks_) {
         callbacks_->clientDisconn(clientId);
      }
   });
}

void HeadlessContainerListener::OnDataFromClient(const std::string &clientId, const std::string &data)
{
   queue_->dispatch([this, clientId, data] {
      headless::RequestPacket packet;
      if (!packet.ParseFromString(data)) {
         logger_->error("[{}] failed to parse request packet", __func__);
         return;
      }

      onRequestPacket(clientId, packet);
   });
}

void HeadlessContainerListener::onClientError(const std::string &clientId, ServerConnectionListener::ClientError errorCode, const Details &details)
{
   switch (errorCode) {
      case ServerConnectionListener::HandshakeFailed: {
         queue_->dispatch([this, details] {
            if (callbacks_) {
               auto ipAddrIt = details.find(Detail::IpAddr);
               callbacks_->terminalHandshakeFailed(ipAddrIt != details.end() ? ipAddrIt->second : "Unknown");
            }
         });
         break;
      }
      default:
         break;
   }
}

bool HeadlessContainerListener::onRequestPacket(const std::string &clientId, headless::RequestPacket packet)
{
   if (!connection_) {
      logger_->error("[HeadlessContainerListener::onRequestPacket] connection is not set");
      return false;
   }

   switch (packet.type()) {
   case headless::AuthenticationRequestType:
      return AuthResponse(clientId, packet);

   case headless::CancelSignTxRequestType:
      return onCancelSignTx(clientId, packet);

   case headless::UpdateDialogDataType:
      return onUpdateDialogData(clientId, packet);

   case headless::SignTxRequestType:
   case headless::SignSettlementTxRequestType:
   case headless::SignPartialTXRequestType:
   case headless::SignSettlementPartialTxType:
   case headless::AutoSignFullType:
      return onSignTxRequest(clientId, packet, packet.type());

   case headless::ResolvePublicSpendersType:
      return onResolvePubSpenders(clientId, packet);

   case headless::CreateHDLeafRequestType:
      return onCreateHDLeaf(clientId, packet);

   case headless::GetHDWalletInfoRequestType:
      return onGetHDWalletInfo(clientId, packet);

   case headless::DisconnectionRequestType:
      break;

   case headless::SyncWalletInfoType:
      return onSyncWalletInfo(clientId, packet);

   case headless::SyncHDWalletType:
      return onSyncHDWallet(clientId, packet);

   case headless::SyncWalletType:
      return onSyncWallet(clientId, packet);

   case headless::SyncCommentType:
      return onSyncComment(clientId, packet);

   case headless::SyncAddressesType:
      return onSyncAddresses(clientId, packet);

   case headless::ExtendAddressChainType:
      return onExtAddrChain(clientId, packet);

   case headless::SyncNewAddressType:
      return onSyncNewAddr(clientId, packet);

   case headless::ExecCustomDialogRequestType:
      return onExecCustomDialog(clientId, packet);

   default:
      logger_->error("[HeadlessContainerListener] unknown request type {}", packet.type());
      return false;
   }
   return true;
}

bool HeadlessContainerListener::AuthResponse(const std::string &clientId, headless::RequestPacket packet)
{
   headless::AuthenticationReply response;
   response.set_authticket("");  // no auth tickets after moving to BIP150/151
   response.set_nettype((netType_ == NetworkType::TestNet) ? headless::TestNetType : headless::MainNetType);

   packet.set_data(response.SerializeAsString());
   bool rc = sendData(packet.SerializeAsString(), clientId);
   logger_->debug("[HeadlessContainerListener] sent auth response");

   if (rc) {
      const auto &priWallet = walletsMgr_->getPrimaryWallet();
      if (priWallet && isAutoSignActive(priWallet->walletId())) {
         headless::AutoSignActEvent autoSignActEvent;
         autoSignActEvent.set_rootwalletid(priWallet->walletId());
         autoSignActEvent.set_errorcode(static_cast<uint>(bs::error::ErrorCode::NoError));

         headless::RequestPacket packet;
         packet.set_type(headless::AutoSignActType);
         packet.set_data(autoSignActEvent.SerializeAsString());
         sendData(packet.SerializeAsString());
      }
   }
   return rc;
}

bool HeadlessContainerListener::onSignTxRequest(const std::string &clientId, const headless::RequestPacket &packet
   , headless::RequestType reqType)
{
   bool partial = (reqType == headless::RequestType::SignPartialTXRequestType)
       || (reqType == headless::RequestType::SignSettlementPartialTxType);

   headless::SignTxRequest request;
   Internal::PasswordDialogDataWrapper dialogData;

   if (reqType == headless::RequestType::SignSettlementTxRequestType
       || reqType == headless::RequestType::SignSettlementPartialTxType){

      headless::SignSettlementTxRequest settlementRequest;

      if (!settlementRequest.ParseFromString(packet.data())) {
         logger_->error("[HeadlessContainerListener] failed to parse SignTxRequest");
         SignTXResponse(clientId, packet.id(), reqType, ErrorCode::FailedToParse);
         return false;
      }

      request = settlementRequest.signtxrequest();
      dialogData = settlementRequest.passworddialogdata();
   }
   else {
      if (!request.ParseFromString(packet.data())) {
         logger_->error("[HeadlessContainerListener] failed to parse SignTxRequest");
         SignTXResponse(clientId, packet.id(), reqType, ErrorCode::FailedToParse);
         return false;
      }
   }

   bs::core::wallet::TXSignRequest txSignReq = bs::signer::pbTxRequestToCore(request, logger_);
   if (!txSignReq.isValid()) {
      logger_->error("[HeadlessContainerListener] invalid SignTxRequest");
      SignTXResponse(clientId, packet.id(), reqType, ErrorCode::TxInvalidRequest);
      return false;
   }

   bool isLegacy = txSignReq.armorySigner_.hasLegacyInputs();

   if (!isLegacy && !partial && txSignReq.txHash.empty()) {
      SPDLOG_LOGGER_ERROR(logger_, "expected tx hash must be set before sign");
      SignTXResponse(clientId, packet.id(), reqType, ErrorCode::TxInvalidRequest);
      return false;
   }

   bs::core::WalletMap wallets;
   std::string rootWalletId;

   for (const auto &walletId : txSignReq.walletIds) {
      const auto wallet = walletsMgr_->getWalletById(walletId);
      if (wallet) {
         wallets[wallet->walletId()] = wallet;
         const auto curRootWalletId = walletsMgr_->getHDRootForLeaf(walletId)->walletId();
         if (!rootWalletId.empty() && (rootWalletId != curRootWalletId)) {
            logger_->error("[HeadlessContainerListener] can't sign leaves from many roots");
            SignTXResponse(clientId, packet.id(), reqType, ErrorCode::WalletAlreadyPresent);
            return false;
         }
         rootWalletId = curRootWalletId;
      }
      else {
         const auto& hdWallet = walletsMgr_->getHDWalletById(walletId);
         if (!hdWallet) {
            logger_->error("[HeadlessContainerListener] failed to find wallet {}", walletId);
            SignTXResponse(clientId, packet.id(), reqType, ErrorCode::WalletNotFound);
            return false;
         }
         if (!rootWalletId.empty()) {
            logger_->error("[HeadlessContainerListener] can't sign leaves from many roots");
            SignTXResponse(clientId, packet.id(), reqType, ErrorCode::WalletAlreadyPresent);
            return false;
         }
         rootWalletId = hdWallet->walletId();
         for (const auto& input : txSignReq.getInputs([](const bs::Address&) { return true; })) {
            const auto& addr = bs::Address::fromUTXO(input);
            const auto& wallet = walletsMgr_->getWalletByAddress(addr);
            if (wallet) {
               wallets[wallet->walletId()] = wallet;
            }
            else {
               if (!partial) {
                  logger_->error("[HeadlessContainerListener] failed to find "
                     "wallet for input address {}", addr.display());
                  SignTXResponse(clientId, packet.id(), reqType, ErrorCode::WalletNotFound);
                  return false;
               }
            }
         }
      }
   }

   if (txSignReq.change.value > 0) {
      // Check that change belongs to same HD wallet
      auto wallet = walletsMgr_->getWalletByAddress(txSignReq.change.address);
      if (!wallet || walletsMgr_->getHDRootForLeaf(wallet->walletId())->walletId() != rootWalletId) {
         logger_->error("[HeadlessContainerListener] invalid change address");
         SignTXResponse(clientId, packet.id(), reqType, ErrorCode::WrongAddress);
         return false;
      }
   }

   if (wallets.empty()) {
      logger_->error("[HeadlessContainerListener] failed to find any wallets");
      SignTXResponse(clientId, packet.id(), reqType, ErrorCode::WalletNotFound);
      return false;
   }

   const auto &ownXbtAddressCb = [this](const bs::Address &address) {
      auto wallet = walletsMgr_->getWalletByAddress(address);
      return wallet && wallet->type() == bs::core::wallet::Type::Bitcoin;
   };
   uint64_t sentAmount = txSignReq.inputAmount(ownXbtAddressCb);
   uint64_t receivedAmount = txSignReq.amountReceived(ownXbtAddressCb);
   uint64_t amount = sentAmount < receivedAmount ? 0 : sentAmount - receivedAmount;

   auto autoSignCategory = static_cast<bs::signer::AutoSignCategory>(dialogData.value<int>(PasswordDialogData::AutoSignCategory));
   const bool autoSign = ((autoSignCategory == bs::signer::AutoSignCategory::SettlementDealer)
      || (reqType == headless::RequestType::AutoSignFullType)) && isAutoSignActive(rootWalletId);

   if (amount && !checkSpendLimit(amount, rootWalletId, autoSign)) {
      SignTXResponse(clientId, packet.id(), reqType, ErrorCode::TxSpendLimitExceed);
      return false;
   }

   auto onPassword = [this, autoSign, wallets, txSignReq, rootWalletId, clientId, id = packet.id(), partial
      , reqType, amount, isLegacy
      , keepDuplicatedRecipients = request.keepduplicatedrecipients()]
      (bs::error::ErrorCode result, const SecureBinaryData &pass)
   {
      if (result == ErrorCode::TxCancelled) {
         logger_->error("[HeadlessContainerListener] transaction cancelled for wallet {}"
            , wallets.cbegin()->second->name());
         SignTXResponse(clientId, id, reqType, result);
         return;
      }

      // check spend limits one more time after password received
      if (!checkSpendLimit(amount, rootWalletId, autoSign)) {
         SignTXResponse(clientId, id, reqType, ErrorCode::TxSpendLimitExceed);
         return;
      }

      const auto rootWallet = walletsMgr_->getHDWalletById(rootWalletId);

      if (rootWallet->isWatchingOnly()) {
         // when signing tx for watching-only wallet we receiving signed tx
         // from signer ui instead of password
         if (rootWallet->isHardwareWallet()) {
            bs::core::WalletPasswordScoped lock(rootWallet, pass);
            //this needs to be a shared_ptr
            auto signReqCopy = txSignReq;
            auto signedTx = rootWallet->signTXRequestWithWallet(signReqCopy);

            if (!isLegacy) {
               try {
                  Tx t(signedTx);
                  if (t.getThisHash() != txSignReq.txHash) {
                     SPDLOG_LOGGER_ERROR(logger_, "unexpected tx hash: {}, expected: {}"
                        , t.getThisHash().toHexStr(true), txSignReq.txHash.toHexStr(true));
                     throw std::logic_error("unexpected tx hash");
                  }
               } catch (const std::exception &e) {
                  SPDLOG_LOGGER_ERROR(logger_, "signed tx verification failed for HW wallet: {}", e.what());
                  SignTXResponse(clientId, id, reqType, ErrorCode::InternalError);
                  return;
               }
            }

            SignTXResponse(clientId, id, reqType, ErrorCode::NoError, signedTx);
         }
         else {
            SignTXResponse(clientId, id, reqType, ErrorCode::NoError, pass);
         }

         if (amount) {
            onXbtSpent(amount, autoSign);
            if (callbacks_) {
               callbacks_->xbtSpent(amount, false);
            }
         }
         return;
      }

      try {
         if (!rootWallet->encryptionTypes().empty() && pass.empty()) {
            logger_->error("[HeadlessContainerListener] empty password for wallet {}"
               , wallets.cbegin()->second->name());
            SignTXResponse(clientId, id, reqType, ErrorCode::MissingPassword);
            return;
         }
         if (wallets.size() == 1) {
            const auto wallet = wallets.cbegin()->second;
            const bs::core::WalletPasswordScoped passLock(rootWallet, pass);
            auto txSignCopy = txSignReq; //TODO: txSignReq should be passed as a shared_ptr instead
            const auto tx = partial ? BinaryData::fromString(wallet->signPartialTXRequest(txSignCopy).SerializeAsString())
               : wallet->signTXRequest(txSignCopy, keepDuplicatedRecipients);
            if (!partial && !isLegacy) {
               Tx t(tx);
               if (t.getThisHash() != txSignReq.txHash) {
                  SPDLOG_LOGGER_ERROR(logger_, "unexpected tx hash: {}, expected: {}"
                     , t.getThisHash().toHexStr(true), txSignReq.txHash.toHexStr(true));
                  throw std::logic_error("unexpected tx hash");
               }
            }
            SignTXResponse(clientId, id, reqType, ErrorCode::NoError, tx);
         }
         else {
            bs::core::wallet::TXMultiSignRequest multiReq;
            multiReq.armorySigner_.merge(txSignReq.armorySigner_);
            multiReq.RBF |= txSignReq.RBF;

            for (unsigned i=0; i<txSignReq.armorySigner_.getTxInCount(); i++) {
               const auto& utxo = txSignReq.armorySigner_.getSpender(i)->getUtxo();
               const auto addr = bs::Address::fromUTXO(utxo);
               const auto wallet = walletsMgr_->getWalletByAddress(addr);
               if (!wallet) {
                  if (!partial) {
                     logger_->error("[{}] failed to find wallet for input address {}"
                        , __func__, addr.display());
                     SignTXResponse(clientId, id, reqType, ErrorCode::WalletNotFound);
                     return;
                  }
               } else {
                  multiReq.addWalletId(wallet->walletId());
               }
            }

            const auto hdWallet = walletsMgr_->getHDWalletById(rootWalletId);
            BinaryData tx;
            {
               const bs::core::WalletPasswordScoped passLock(rootWallet, pass);
               tx = bs::core::SignMultiInputTX(multiReq, wallets, partial);
               if (!partial && !isLegacy) {
                  Tx t(tx);
                  if (t.getThisHash() != txSignReq.txHash) {
                     SPDLOG_LOGGER_ERROR(logger_, "unexpected tx hash: {}, expected: {}"
                        , t.getThisHash().toHexStr(true), txSignReq.txHash.toHexStr(true));
                     throw std::logic_error("unexpected tx hash");
                  }
               }
            }
            SignTXResponse(clientId, id, reqType, ErrorCode::NoError, tx);
         }

         onXbtSpent(amount, autoSign);
         if (callbacks_) {
            callbacks_->xbtSpent(amount, autoSign);
         }
      }
      catch (const std::exception &e) {
         logger_->error("[HeadlessContainerListener] failed to sign {} TX request: {}", partial ? "partial" : "full", e.what());
         if (invalidPasswordError(e)) {
            SignTXResponse(clientId, id, reqType, ErrorCode::InvalidPassword);
         } else {
            SignTXResponse(clientId, id, reqType, ErrorCode::InternalError);
         }
         passwords_.erase(rootWalletId);
      }
   };

   dialogData.insert(PasswordDialogData::WalletId, rootWalletId);
   auto data = request.SerializeAsString();
   dialogData.insert(PasswordDialogData::TxRequest, data.c_str(), data.size());

   return RequestPasswordIfNeeded(clientId, rootWalletId, txSignReq, reqType, dialogData, onPassword);
}

bool HeadlessContainerListener::onCancelSignTx(const std::string &, headless::RequestPacket packet)
{
   headless::CancelSignTx request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[HeadlessContainerListener] failed to parse CancelSignTx");
      return false;
   }

   if (callbacks_) {
      callbacks_->cancelTxSign(BinaryData::fromString(request.tx_id()));
   }

   return true;
}

bool HeadlessContainerListener::onUpdateDialogData(const std::string &clientId, headless::RequestPacket packet)
{
   headless::UpdateDialogDataRequest request;

   if (!request.ParseFromString(packet.data())) {
      logger_->error("[{}] failed to parse request", __func__);
      return false;
   }
   Internal::PasswordDialogDataWrapper updatedDialogData = request.passworddialogdata();
   const auto &id = updatedDialogData.value<std::string>(PasswordDialogData::SettlementId);

   logger_->debug("[{}] Requested dialog data update for settl id {}", __func__, id);

   if (id.empty()) {
      return true;
   }

   // try to find dialog in queued dialogs deferredPasswordRequests_
   auto it = deferredPasswordRequests_.begin();
   while (it != deferredPasswordRequests_.end()) {
      if (it->dialogData.value<std::string>(PasswordDialogData::SettlementId) == id) {
         logger_->debug("[{}] Updating dialog data for settl id {}", __func__, id);

         for (auto & pair : request.passworddialogdata().valuesmap())
         {
             (*it->dialogData.mutable_valuesmap())[pair.first] = pair.second;
         }
      }

      it++;
   }

   if (callbacks_) {
      callbacks_->updateDialogData(request.passworddialogdata());
   }
   return true;
}

#if 0
bool HeadlessContainerListener::onSignSettlementPayoutTxRequest(const std::string &clientId
   , const headless::RequestPacket &packet)
{
   const auto reqType = headless::SignSettlementPayoutTxType;
   headless::SignSettlementPayoutTxRequest request;

   if (!request.ParseFromString(packet.data())) {
      logger_->error("[{}] failed to parse request", __func__);
      SignTXResponse(clientId, packet.id(), reqType, ErrorCode::FailedToParse);
      return false;
   }

   Internal::PasswordDialogDataWrapper dialogData = request.passworddialogdata();
   dialogData.insert(PasswordDialogData::PayOutType, true);

   bs::core::wallet::TXSignRequest txSignReq;
   txSignReq.walletIds = { walletsMgr_->getPrimaryWallet()->walletId() };

   Codec_SignerState::SignerState msgSignerState;
   if (!msgSignerState.ParseFromString(request.signpayouttxrequest().signerstate())) {
      logger_->error("[{}] failed to parse signer state", __func__);
      SignTXResponse(clientId, packet.id(), reqType, ErrorCode::FailedToParse);
      return false;
   }
   txSignReq.armorySigner_.deserializeState(msgSignerState);

   txSignReq.fee = request.signpayouttxrequest().fee();
   txSignReq.txHash = BinaryData::fromString(request.signpayouttxrequest().tx_hash());

   if (!txSignReq.isValid()) {
      logger_->error("[HeadlessContainerListener] invalid SignTxRequest");
      SignTXResponse(clientId, packet.id(), reqType, ErrorCode::TxInvalidRequest);
      return false;
   }

   if (txSignReq.txHash.empty()) {
      SPDLOG_LOGGER_ERROR(logger_, "expected tx hash must be set before sign");
      SignTXResponse(clientId, packet.id(), reqType, ErrorCode::TxInvalidRequest);
      return false;
   }

   const auto settlData = request.signpayouttxrequest().settlement_data();
   bs::core::wallet::SettlementData sd{ BinaryData::fromString(settlData.settlement_id())
      , BinaryData::fromString(settlData.counterparty_pubkey()), settlData.my_pubkey_first() };

   const auto onPassword = [this, txSignReq, sd, clientId, id = packet.id(), reqType]
      (bs::error::ErrorCode result, const SecureBinaryData &pass) {
      if (result != ErrorCode::NoError) {
         logger_->error("[HeadlessContainerListener] payout transaction failed, result from ui: {}", static_cast<int>(result));
         SignTXResponse(clientId, id, reqType, result);
         return;
      }

      try {
         const auto wallet = walletsMgr_->getPrimaryWallet();
         if (!wallet->encryptionTypes().empty() && pass.empty()) {
            logger_->error("[HeadlessContainerListener] empty password for wallet {}", wallet->name());
            SignTXResponse(clientId, id, reqType, ErrorCode::MissingPassword);
            return;
         }
         {
            const bs::core::WalletPasswordScoped passLock(wallet, pass);
            auto txSignCopy = txSignReq; //TODO: txSignReq should be a shared_ptr
            const auto tx = wallet->signSettlementTXRequest(txSignCopy, sd);
            SignTXResponse(clientId, id, reqType, ErrorCode::NoError, tx);
         }
      } catch (const std::exception &e) {
         logger_->error("[HeadlessContainerListener] failed to sign payout TX request: {}", e.what());
         if (invalidPasswordError(e)) {
            SignTXResponse(clientId, id, reqType, ErrorCode::InvalidPassword);
         } else {
            SignTXResponse(clientId, id, reqType, ErrorCode::InternalError);
         }
      }
   };

   return RequestPasswordIfNeeded(clientId, txSignReq.walletIds.front(), txSignReq, reqType
      , dialogData, onPassword);
}

bool HeadlessContainerListener::onSignAuthAddrRevokeRequest(const std::string &clientId
   , const headless::RequestPacket &packet)
{
   headless::SignAuthAddrRevokeRequest request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[{}] failed to parse request", __func__);
      SignTXResponse(clientId, packet.id(), packet.type(), ErrorCode::FailedToParse);
      return false;
   }

   const auto wallet = walletsMgr_->getWalletById(request.wallet_id());
   if (!wallet) {
      logger_->error("[{}] failed to find auth wallet {}", __func__, request.wallet_id());
      SignTXResponse(clientId, packet.id(), packet.type(), ErrorCode::WalletNotFound);
      return false;
   }

   Internal::PasswordDialogDataWrapper dialogData;
   dialogData.insert(PasswordDialogData::WalletId, request.wallet_id());
   dialogData.insert(PasswordDialogData::AuthAddress, request.auth_address());

   bs::core::wallet::TXSignRequest txSignReq;
   txSignReq.walletIds = { wallet->walletId() };

   UTXO utxo;
   utxo.unserialize(BinaryData::fromString(request.utxo()));
   if (utxo.isInitialized()) {
      auto spender = std::make_shared<Armory::Signer::ScriptSpender>(utxo);
      txSignReq.armorySigner_.addSpender(spender);
   }
   else {
      logger_->error("[{}] failed to parse UTXO", __func__);
      SignTXResponse(clientId, packet.id(), packet.type(), ErrorCode::TxInvalidRequest);
      return false;
   }

   const auto onPassword = [this, wallet, utxo, request, clientId, id = packet.id(), reqType = packet.type()]
   (bs::error::ErrorCode result, const SecureBinaryData &pass) {
      if (result != ErrorCode::NoError) {
         logger_->error("[HeadlessContainerListener] auth revoke failed, result from ui: {}", static_cast<int>(result));
         SignTXResponse(clientId, id, reqType, result);
         return;
      }

      try {
         const bs::core::WalletPasswordScoped passLock(walletsMgr_->getPrimaryWallet(), pass);
         const auto lock = wallet->lockDecryptedContainer();
         auto authAddr = bs::Address::fromAddressString(request.auth_address());
         auto validationAddr = bs::Address::fromAddressString(request.validation_address());
         const auto tx = AuthAddressLogic::revoke(authAddr, wallet->getResolver()
            , validationAddr, utxo);
         SignTXResponse(clientId, id, reqType, ErrorCode::NoError, tx);
      } catch (const std::exception &e) {
         logger_->error("[HeadlessContainerListener] failed to sign addr revoke TX request: {}", e.what());
         SignTXResponse(clientId, id, reqType, ErrorCode::InternalError);
      }
   };

   return RequestPasswordIfNeeded(clientId, txSignReq.walletIds.front(), txSignReq
      , packet.type(), dialogData, onPassword);
}
#endif   //0

bool HeadlessContainerListener::onResolvePubSpenders(const std::string &clientId
   , const headless::RequestPacket &packet)
{
   headless::SignTxRequest request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[{}] failed to parse request", __func__);
      SignTXResponse(clientId, packet.id(), packet.type(), ErrorCode::FailedToParse);
      return false;
   }

   bs::core::wallet::TXSignRequest txSignReq = bs::signer::pbTxRequestToCore(request, logger_);
   if (txSignReq.armorySigner_.getTxInCount() == 0) {
      logger_->error("[HeadlessContainerListener::onResolvePubSpenders] invalid SignTxRequest");
      SignTXResponse(clientId, packet.id(), packet.type(), ErrorCode::TxInvalidRequest);
      return false;
   }

   std::set<std::shared_ptr<bs::core::Wallet>> wallets;  // all wallets that participate in spenders resolution
   for (const auto &walletId : txSignReq.walletIds) {
      const auto &wallet = walletsMgr_->getWalletById(walletId);
      if (!wallet) {
         logger_->error("[HeadlessContainerListener::onResolvePubSpenders] failed"
            " to find wallet by id {}", walletId);
         continue;
      }
      wallets.insert(wallet);
   }
   for (const auto &wallet : wallets) {
      txSignReq.resolveSpenders(wallet->getPublicResolver());
   }
   const auto &resolvedState = txSignReq.serializeState();
   if (!resolvedState.IsInitialized()) {
      SignTXResponse(clientId, packet.id(), packet.type(), ErrorCode::InternalError);
      return false;
   }
   SignTXResponse(clientId, packet.id(), packet.type(), ErrorCode::NoError
      , BinaryData::fromString(resolvedState.SerializeAsString()));
   return true;
}

void HeadlessContainerListener::SignTXResponse(const std::string &clientId, unsigned int id, headless::RequestType reqType
   , bs::error::ErrorCode errorCode, const BinaryData &tx)
{
   headless::SignTxReply response;
   response.set_errorcode(static_cast<uint32_t>(errorCode));

   if (!tx.empty()) {
      response.set_signedtx(tx.toBinStr());
   }

   headless::RequestPacket packet;
   packet.set_id(id);
   packet.set_type(reqType);
   packet.set_data(response.SerializeAsString());

   if (!sendData(packet.SerializeAsString(), clientId)) {
      logger_->error("[HeadlessContainerListener] failed to send response signTX packet");
   }
   if (reqType == headless::ResolvePublicSpendersType) {
      return;
   }
   if (errorCode == bs::error::ErrorCode::NoError && callbacks_) {
      callbacks_->txSigned(tx);
   }
}

void HeadlessContainerListener::passwordReceived(const std::string &clientId, const std::string &walletId
   , bs::error::ErrorCode result, const SecureBinaryData &password)
{
   if (deferredPasswordRequests_.empty()) {
      logger_->error("[HeadlessContainerListener::{}] failed to find password received callback", __func__);
      return;
   }
   const PasswordReceivedCb &cb = std::move(deferredPasswordRequests_.front().callback);
   if (cb) {
      cb(result, password);
   }

   // at this point password workflow finished for deferredPasswordRequests_.front() dialog
   // now we can remove dialog
   deferredPasswordRequests_.erase(deferredPasswordRequests_.begin());
   deferredDialogRunning_ = false;

   // execute next pw dialog
   RunDeferredPwDialog();
}

void HeadlessContainerListener::passwordReceived(const std::string &walletId
   , bs::error::ErrorCode result, const SecureBinaryData &password)
{
   passwordReceived({}, walletId, result, password);
}

bool HeadlessContainerListener::RequestPasswordIfNeeded(const std::string &clientId, const std::string &walletId
   , const bs::core::wallet::TXSignRequest &txReq
   , headless::RequestType reqType, const Internal::PasswordDialogDataWrapper &dialogData
   , const PasswordReceivedCb &cb)
{
   std::string rootId = walletId;
   const auto wallet = walletsMgr_->getWalletById(walletId);
   bool needPassword = true;
   if (wallet) {
      const auto &hdRoot = walletsMgr_->getHDRootForLeaf(walletId);
      if (hdRoot) {
         rootId = hdRoot->walletId();
         needPassword = !hdRoot->encryptionTypes().empty() || hdRoot->isWatchingOnly();
      }
   }
   else {
      const auto hdWallet = walletsMgr_->getHDWalletById(walletId);
      if (!hdWallet) {
         logger_->error("[{}] failed to find wallet {}", __func__, walletId);
         return false;
      }
      needPassword = !hdWallet->encryptionTypes().empty() || hdWallet->isWatchingOnly();
   }

   auto autoSignCategory = static_cast<bs::signer::AutoSignCategory>(dialogData.value<int>(PasswordDialogData::AutoSignCategory));
   // currently only dealer can use autosign
   bool autoSignAllowed = ((autoSignCategory == bs::signer::AutoSignCategory::SettlementDealer)
      || (reqType == headless::RequestType::AutoSignFullType));

   SecureBinaryData password;
   if (autoSignAllowed && needPassword) {
      const auto passwordIt = passwords_.find(rootId);
      if (passwordIt != passwords_.end()) {
         needPassword = false;
         password = passwordIt->second;
      }
   }

   if (!needPassword) {
      if (cb) {
         cb(ErrorCode::NoError, password);
      }
      return true;
   }

   return RequestPassword(rootId, txReq, reqType, dialogData, cb);
}

bool HeadlessContainerListener::RequestPasswordsIfNeeded(int reqId, const std::string &clientId
   , const bs::core::wallet::TXMultiSignRequest &txMultiReq, const bs::core::WalletMap &walletMap
   , const PasswordsReceivedCb &cb)
{
   Internal::PasswordDialogDataWrapper dialogData;

   TempPasswords tempPasswords;
   for (const auto &wallet : walletMap) {
      const auto &walletId = wallet.first;
      const auto &rootWallet = walletsMgr_->getHDRootForLeaf(walletId);
      const auto &rootWalletId = rootWallet->walletId();

      tempPasswords.rootLeaves[rootWalletId].insert(walletId);
      tempPasswords.reqWalletIds.insert(walletId);

      if (!rootWallet->encryptionTypes().empty()) {
         const auto cbWalletPass = [this, reqId, cb, rootWalletId](bs::error::ErrorCode result, const SecureBinaryData &password) {
            auto &tempPasswords = tempPasswords_[reqId];
            const auto &walletsIt = tempPasswords.rootLeaves.find(rootWalletId);
            if (walletsIt == tempPasswords.rootLeaves.end()) {
               return;
            }
            for (const auto &walletId : walletsIt->second) {
               tempPasswords.passwords[walletId] = password;
            }
            if (tempPasswords.passwords.size() == tempPasswords.reqWalletIds.size()) {
               cb(tempPasswords.passwords);
               tempPasswords_.erase(reqId);
            }
         };

         bs::core::wallet::TXSignRequest txReq;
         txReq.walletIds = { rootWallet->walletId() };
         RequestPassword(clientId, txReq, headless::RequestType::SignTxRequestType, dialogData, cbWalletPass);
      }
      else {
         tempPasswords.passwords[walletId] = {};
      }
   }
   if (tempPasswords.reqWalletIds.size() == tempPasswords.passwords.size()) {
      cb(tempPasswords.passwords);
   }
   else {
      tempPasswords_[reqId] = tempPasswords;
   }
   return true;
}

bool HeadlessContainerListener::RequestPassword(const std::string &rootId, const bs::core::wallet::TXSignRequest &txReq
   , headless::RequestType reqType, const Internal::PasswordDialogDataWrapper &dialogData
   , const PasswordReceivedCb &cb)
{
   // TODO:
   // if deferredDialogRunning_ is set to true, no one else pw dialog might be displayed
   // need to implement some timer which will control dialogs queue for case when proxyCallback not fired
   // and deferredDialogRunning_ flag not cleared

   PasswordRequest dialog;

   dialog.dialogData = dialogData;

   milliseconds durationTotal = milliseconds(dialogData.value<int>(PasswordDialogData::DurationTotal));
   if (durationTotal == 0s) {
      durationTotal = kDefaultDuration;
   }
   dialog.dialogExpirationTime = std::chrono::steady_clock::now() + durationTotal;

   dialog.callback = cb;
   dialog.passwordRequest = [this, reqType, txReq](const Internal::PasswordDialogDataWrapper &dlgData){
      if (callbacks_) {
         switch (reqType) {
         case headless::SignTxRequestType:
            callbacks_->decryptWalletRequest(signer::PasswordDialogType::SignTx, dlgData, txReq);
            break;
         case headless::SignPartialTXRequestType:
            callbacks_->decryptWalletRequest(signer::PasswordDialogType::SignPartialTx, dlgData, txReq);
            break;

         case headless::SignSettlementTxRequestType:
         case headless::SignSettlementPayoutTxType:
            callbacks_->decryptWalletRequest(signer::PasswordDialogType::SignSettlementTx, dlgData, txReq);
            break;
         case headless::SignSettlementPartialTxType:
            callbacks_->decryptWalletRequest(signer::PasswordDialogType::SignSettlementPartialTx, dlgData, txReq);
            break;

         case headless::CreateHDLeafRequestType:
            callbacks_->decryptWalletRequest(signer::PasswordDialogType::CreateHDLeaf, dlgData);
            break;
         case headless::CreateSettlWalletType:
            callbacks_->decryptWalletRequest(signer::PasswordDialogType::CreateSettlementLeaf, dlgData);
            break;
         case headless::SetUserIdType:
            callbacks_->decryptWalletRequest(signer::PasswordDialogType::CreateAuthLeaf, dlgData, txReq);
            break;
         case headless::SignAuthAddrRevokeType:
            callbacks_->decryptWalletRequest(signer::PasswordDialogType::RevokeAuthAddress, dlgData, txReq);
            break;
         case headless::EnableTradingInWalletType:
            callbacks_->decryptWalletRequest(signer::PasswordDialogType::EnableTrading, dlgData);
            break;
         case headless::PromoteWalletToPrimaryType:
            callbacks_->decryptWalletRequest(signer::PasswordDialogType::PromoteToPrimary, dlgData);
            break;

         default:
            logger_->warn("[{}] unknown request for password request: {}", __func__, (int)reqType);
         }
      }
   };

   deferredPasswordRequests_.push_back(dialog);
   RunDeferredPwDialog();

   return true;
}

void HeadlessContainerListener::RunDeferredPwDialog()
{
   if (deferredPasswordRequests_.empty()) {
      return;
   }

   if(!deferredDialogRunning_) {
      deferredDialogRunning_ = true;

      std::sort(deferredPasswordRequests_.begin(), deferredPasswordRequests_.end());
      const auto &dialog = deferredPasswordRequests_.front();

      auto dialogData = dialog.dialogData;
      milliseconds remainingDuration = std::chrono::duration_cast<milliseconds>(dialog.dialogExpirationTime - std::chrono::steady_clock::now());

      if (remainingDuration < seconds(3)) {
         // Don't display dialog if it will be expired soon or already expired
         const PasswordReceivedCb &cb = std::move(deferredPasswordRequests_.front().callback);
         if (cb) {
            cb(bs::error::ErrorCode::TxCancelled, {});
         }

         // at this point password workflow finished for deferredPasswordRequests_.front() dialog
         // now we can remove dialog
         deferredPasswordRequests_.erase(deferredPasswordRequests_.begin());
         deferredDialogRunning_ = false;

         // execute next pw dialog
         RunDeferredPwDialog();
      }
      else {
         dialogData.insert(PasswordDialogData::DurationLeft, static_cast<int>(remainingDuration.count()));
         deferredPasswordRequests_.front().passwordRequest(dialogData); // run stored lambda
      }
   }
}

#if 0 // trading is being removed
bool HeadlessContainerListener::onSetUserId(const std::string &clientId, headless::RequestPacket &packet)
{
   headless::SetUserIdRequest request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[HeadlessContainerListener] failed to parse SetUserIdRequest");
      return false;
   }

   if (request.userid().empty()) {
      logger_->info("[{}] empty user id - do nothing", __func__);
      return true;
   }

   walletsMgr_->setUserId(BinaryData::fromString(request.userid()));

   const auto wallet = walletsMgr_->getPrimaryWallet();
   if (!wallet) {
      logger_->info("[{}] no primary wallet - aborting", __func__);
      return true;
   }
   const auto group = wallet->getGroup(bs::hd::BlockSettle_Auth);
   if (!group) {
      logger_->error("[{}] primary wallet misses Auth group", __func__);
      setUserIdResponse(clientId, packet.id(), headless::AWR_NoPrimary);
      return false;
   }
   const auto authGroup = std::dynamic_pointer_cast<bs::core::hd::AuthGroup>(group);
   if (!authGroup) {
      logger_->error("[{}] Auth group has wrong type", __func__);
      setUserIdResponse(clientId, packet.id(), headless::AWR_NoPrimary);
      return false;
   }
   const auto salt = SecureBinaryData::fromString(request.userid());

   if (salt.empty()) {
      logger_->debug("[{}] unsetting auth salt", __func__);
      setUserIdResponse(clientId, packet.id(), headless::AWR_UnsetSalt);
      return true;
   }

   logger_->debug("[{}] setting salt {}...", __func__, salt.toHexStr());
   const auto prevSalt = authGroup->getSalt();
   if (prevSalt.empty()) {
      try {
         authGroup->setSalt(salt);
      } catch (const std::exception &e) {
         logger_->error("[{}] error setting auth salt: {}", __func__, e.what());
         setUserIdResponse(clientId, packet.id(), headless::AWR_SaltSetFailed);
         return false;
      }
   }
   else {
      if (prevSalt == salt) {
         logger_->debug("[{}] salts match - ok", __func__);
      }
      else {
         logger_->error("[{}] salts don't match - aborting for now", __func__);
         setUserIdResponse(clientId, packet.id(), headless::AWR_WrongSalt);
         return false;
      }
   }

   const bs::hd::Path authPath({bs::hd::Purpose::Native, bs::hd::BlockSettle_Auth, 0});
   auto leaf = authGroup->getLeafByPath(authPath);
   if (leaf) {
      const auto authLeaf = std::dynamic_pointer_cast<bs::core::hd::AuthLeaf>(leaf);
      if (authLeaf && (authLeaf->getSalt() == salt)) {
         setUserIdResponse(clientId, packet.id(), headless::AWR_NoError, authLeaf->walletId());
         return true;
      }
      else {
         setUserIdResponse(clientId, packet.id(), headless::AWR_WrongSalt, leaf->walletId());
         return false;
      }
   }
   setUserIdResponse(clientId, packet.id(), headless::AWR_NoPrimary);
   return true;
}

bool HeadlessContainerListener::onSyncCCNames(headless::RequestPacket &packet)
{
   headless::SyncCCNamesData request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[{}] failed to parse request", __func__);
      if (callbacks_) {
         callbacks_->ccNamesReceived(false);
      }
      return false;
   }
   logger_->debug("[{}] received {} CCs", __func__, request.ccnames_size());
   std::vector<std::string> ccNames;
   for (int i = 0; i < request.ccnames_size(); ++i) {
      const auto cc = request.ccnames(i);
      ccNames.emplace_back(std::move(cc));
   }
   if (callbacks_) {
      callbacks_->ccNamesReceived(true);
   }
   walletsMgr_->setCCLeaves(ccNames);
   return true;
}

void HeadlessContainerListener::setUserIdResponse(const std::string &clientId, unsigned int id
   , headless::AuthWalletResponseType respType, const std::string &walletId)
{
   headless::SetUserIdResponse response;
   response.set_auth_wallet_id(walletId);
   response.set_response(respType);

   headless::RequestPacket packet;
   packet.set_id(id);
   packet.set_type(headless::SetUserIdType);
   packet.set_data(response.SerializeAsString());
   sendData(packet.SerializeAsString(), clientId);
}
#endif   //0

bool HeadlessContainerListener::onCreateHDLeaf(const std::string &clientId
   , Blocksettle::Communication::headless::RequestPacket &packet)
{
   headless::CreateHDLeafRequest request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[HeadlessContainerListener] failed to parse CreateHDLeafRequest");
      return false;
   }

   const auto hdWallet = walletsMgr_->getHDWalletById(request.rootwalletid());
   if (!hdWallet) {
      logger_->error("[HeadlessContainerListener] failed to find root HD wallet by id {}", request.rootwalletid());
      CreateHDLeafResponse(clientId, packet.id(), ErrorCode::WalletNotFound);
      return false;
   }
   const auto path = bs::hd::Path::fromString(request.path());
   if ((path.length() < 3) && !path.isAbsolute()) {
      logger_->error("[HeadlessContainerListener] invalid path {} at HD wallet creation", request.path());
      CreateHDLeafResponse(clientId, packet.id(), ErrorCode::InternalError);
      return false;
   }

   const auto onPassword = [this, hdWallet, path, clientId, id = packet.id(), salt=SecureBinaryData::fromString(request.salt())]
      (bs::error::ErrorCode result, const SecureBinaryData &pass)
   {
      std::shared_ptr<bs::core::hd::Node> leafNode;
      if (result != ErrorCode::NoError) {
         logger_->error("[HeadlessContainerListener] no password for encrypted wallet");
         CreateHDLeafResponse(clientId, id, result);
         return;
      }

      const auto groupIndex = static_cast<bs::hd::CoinType>(path.get(1));
      auto group = hdWallet->getGroup(groupIndex);
      if (!group) {
         group = hdWallet->createGroup(groupIndex);
      }

      try {
         if (!salt.empty()) {
            const auto authGroup = std::dynamic_pointer_cast<bs::core::hd::AuthGroup>(group);
            if (authGroup) {
               const auto prevSalt = authGroup->getSalt();
               if (prevSalt.empty()) {
                  authGroup->setSalt(salt);
               }
               else if (prevSalt != salt) {
                  logger_->error("[HeadlessContainerListener] auth salts mismatch");
                  CreateHDLeafResponse(clientId, id, ErrorCode::MissingAuthKeys);
                  return;
               }
            }
         }

         auto leaf = group->getLeafByPath(path);

         if (leaf == nullptr) {
            const bs::core::WalletPasswordScoped lock(hdWallet, pass);
            leaf = group->createLeaf(path);

            if (leaf == nullptr) {
               logger_->error("[HeadlessContainerListener] failed to create/get leaf {}", path.toString());
               CreateHDLeafResponse(clientId, id, ErrorCode::InternalError);
               return;
            }

            if (callbacks_) {
               callbacks_->walletChanged(leaf->walletId());
            }
#if 0 // XBT settlement is being removed
            if ((path.get(1) | bs::hd::hardFlag) == bs::hd::CoinType::BlockSettle_Auth) {
               for (int i = 0; i < 10; i++) {
                  leaf->getNewExtAddress();
               }
               createSettlementLeaves(hdWallet, leaf->getUsedAddressList());
            }
#endif
         }

         auto assetPtr = leaf->getRootAsset();

         auto rootPtr = std::dynamic_pointer_cast<Armory::Assets::AssetEntry_BIP32Root>(assetPtr);
         if (rootPtr == nullptr) {
            throw Armory::Assets::AssetException("unexpected root asset type");
         }

         CreateHDLeafResponse(clientId, id, ErrorCode::NoError, leaf);
      }
      catch (const std::exception &e) {
         logger_->error("[HeadlessContainerListener::CreateHDLeaf] failed: {}", e.what());
         CreateHDLeafResponse(clientId, id, ErrorCode::WalletNotFound);
      }
   };

   RequestPasswordIfNeeded(clientId, request.rootwalletid(), {}, headless::CreateHDLeafRequestType, request.passworddialogdata(), onPassword);
   return true;
}

#if 0 // XBT settlement is being removed
bool HeadlessContainerListener::createSettlementLeaves(const std::shared_ptr<bs::core::hd::Wallet> &wallet
   , const std::vector<bs::Address> &authAddresses)
{
   if (!wallet) {
      return false;
   }
   logger_->debug("[{}] creating settlement leaves for {} auth address[es]", __func__
      , authAddresses.size());
   for (const auto &authAddr : authAddresses) {
      try {
         const auto leaf = wallet->createSettlementLeaf(authAddr);
         if (!leaf) {
            logger_->error("[{}] failed to create settlement leaf for {}"
               , __func__, authAddr.display());
            return false;
         }
         if (callbacks_) {
            callbacks_->walletChanged(leaf->walletId());
         }
      }
      catch (const std::exception &e) {
         logger_->error("[{}] failed to create settlement leaf for {}: {}", __func__
            , authAddr.display(), e.what());
         return false;
      }
   }
   return true;
}

bool HeadlessContainerListener::createAuthLeaf(const std::shared_ptr<bs::core::hd::Wallet> &wallet
   , const BinaryData &salt)
{
   if (salt.empty()) {
      logger_->error("[{}] can't create auth leaf with empty salt", __func__);
      return false;
   }
   const auto group = wallet->getGroup(bs::hd::BlockSettle_Auth);
   if (!group) {
      logger_->error("[{}] primary wallet misses Auth group", __func__);
      return false;
   }
   const auto authGroup = std::dynamic_pointer_cast<bs::core::hd::AuthGroup>(group);
   if (!authGroup) {
      logger_->error("[{}] Auth group has wrong type", __func__);
      return false;
   }

   const auto prevSalt = authGroup->getSalt();
   if (prevSalt.empty()) {
      try {
         authGroup->setSalt(salt);
      } catch (const std::exception &e) {
         logger_->error("[{}] error setting auth salt: {}", __func__, e.what());
         return false;
      }
   } else {
      if (prevSalt != salt) {
         logger_->error("[{}] salts don't match - aborting", __func__);
         return false;
      }
   }

   const bs::hd::Path authPath({ bs::hd::Purpose::Native, bs::hd::BlockSettle_Auth, 0 });
   auto leaf = authGroup->getLeafByPath(authPath);
   if (leaf) {
      const auto authLeaf = std::dynamic_pointer_cast<bs::core::hd::AuthLeaf>(leaf);
      if (authLeaf && (authLeaf->getSalt() == salt)) {
         logger_->debug("[{}] auth leaf for {} aready exists", __func__, salt.toHexStr());
         return true;
      } else {
         logger_->error("[{}] auth leaf salts mismatch", __func__);
         return false;
      }
   }

   try {
      auto leaf = group->createLeaf(AddressEntryType_Default, 0 + bs::hd::hardFlag, 5);
      if (leaf) {
         for (int i = 0; i < 5; i++) {
            leaf->getNewExtAddress();
         }
         //return createSettlementLeaves(wallet, leaf->getUsedAddressList());
         return true;
      } else {
         logger_->error("[HeadlessContainerListener::onSetUserId] failed to create auth leaf");
      }
   } catch (const std::exception &e) {
      logger_->error("[HeadlessContainerListener::onSetUserId] failed to create auth leaf: {}", e.what());
   }
   return false;
}

bool HeadlessContainerListener::onEnableTradingInWallet(const std::string& clientId, headless::RequestPacket& packet)
{
   headless::EnableTradingInWalletRequest request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[HeadlessContainerListener::onEnableTradingInWallet] failed to parse EnableTradingInWalletRequest");
      return false;
   }

   const std::string &walletId = request.rootwalletid();
   const auto hdWallet = walletsMgr_->getHDWalletById(walletId);
   if (!hdWallet) {
      logger_->error("[HeadlessContainerListener::onEnableTradingInWallet] failed to find root HD wallet by id {}", walletId);
      CreateEnableTradingResponse(clientId, packet.id(), ErrorCode::WalletNotFound, walletId);
      return false;
   }

   const auto onPassword = [this, hdWallet, walletId, clientId, id = packet.id(), userId=request.user_id()]
      (bs::error::ErrorCode result, const SecureBinaryData &pass)
   {
      std::shared_ptr<bs::core::hd::Node> leafNode;
      if (result != ErrorCode::NoError) {
         logger_->error("[HeadlessContainerListener::onEnableTradingInWallet] no password for encrypted wallet");
         CreateEnableTradingResponse(clientId, id, result, walletId);
         return;
      }

      auto group = hdWallet->getGroup(bs::hd::BlockSettle_Auth);
      if (!group) {
         group = hdWallet->createGroup(bs::hd::BlockSettle_Auth);
      }

      const bs::core::WalletPasswordScoped lock(hdWallet, pass);
      if (!createAuthLeaf(hdWallet, BinaryData::fromString(userId))) {
         logger_->error("[HeadlessContainerListener::onEnableTradingInWallet] failed to create auth leaf");
      }

      if (!walletsMgr_->ccLeaves().empty()) {
         logger_->debug("[HeadlessContainerListener::onEnableTradingInWallet] creating {} CC leaves"
            , walletsMgr_->ccLeaves().size());
         group = hdWallet->createGroup(bs::hd::BlockSettle_CC);
         if (group) {
            for (const auto &cc : walletsMgr_->ccLeaves()) {
               try {
                  group->createLeaf(AddressEntryType_P2WPKH, cc);
               }  // existing leaf creation failure is ignored
               catch (...) {}
            }
         }
         else {
            logger_->error("[HeadlessContainerListener::onEnableTradingInWallet] failed to create CC group");
         }
      }
      CreateEnableTradingResponse(clientId, id, ErrorCode::NoError, walletId);
      walletsListUpdated();
   };

   RequestPasswordIfNeeded(clientId, request.rootwalletid(), {}, headless::EnableTradingInWalletType
      , request.passworddialogdata(), onPassword);
   return true;
}

bool HeadlessContainerListener::onPromoteWalletToPrimary(const std::string& clientId, Blocksettle::Communication::headless::RequestPacket& packet)
{
   headless::PromoteWalletToPrimaryRequest request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[HeadlessContainerListener::onPromoteWalletToPrimary] failed to parse PromoteWalletToPrimaryRequest");
      return false;
   }

   const std::string &walletId = request.rootwalletid();

   if (walletsMgr_->getPrimaryWallet() != nullptr) {
      logger_->error("[HeadlessContainerListener::onPromoteWalletToPrimary] primary wallet already exists");
      CreatePromoteWalletResponse(clientId, packet.id(), ErrorCode::WalletAlreadyPresent, walletId);
      return false;
   }

   const auto hdWallet = walletsMgr_->getHDWalletById(walletId);
   if (!hdWallet) {
      logger_->error("[HeadlessContainerListener::onPromoteWalletToPrimary] failed to find root HD wallet by id {}", walletId);
      CreatePromoteWalletResponse(clientId, packet.id(), ErrorCode::WalletNotFound, walletId);
      return false;
   }

   const auto onPassword = [this, hdWallet, walletId, clientId, id = packet.id()]
      (bs::error::ErrorCode result, const SecureBinaryData &pass)
   {
      if (result != ErrorCode::NoError) {
         logger_->error("[HeadlessContainerListener::onPromoteWalletToPrimary] no password for encrypted wallet");
         CreatePromoteWalletResponse(clientId, id, result, walletId);
         return;
      }

      walletsMgr_->UpdateWalletToPrimary(hdWallet, pass);
      CreatePromoteWalletResponse(clientId, id, ErrorCode::NoError, walletId);
      walletsListUpdated();
   };

   RequestPasswordIfNeeded(clientId, request.rootwalletid(), {}, headless::PromoteWalletToPrimaryType
      , request.passworddialogdata(), onPassword);
   return true;
}
#endif   //0

void HeadlessContainerListener::CreateHDLeafResponse(const std::string &clientId, unsigned int id
   , ErrorCode result, const std::shared_ptr<bs::core::hd::Leaf>& leaf)
{
   headless::CreateHDLeafResponse response;
   if (result == bs::error::ErrorCode::NoError && leaf) {
      const std::string pathString = leaf->path().toString();
      logger_->debug("[HeadlessContainerListener::CreateHDLeafResponse] : {} {}"
         , pathString, leaf->walletId());

      auto leafResponse = response.mutable_leaf();

      leafResponse->set_path(pathString);
      leafResponse->set_walletid(leaf->walletId());
   }
   response.set_errorcode(static_cast<uint32_t>(result));

   headless::RequestPacket packet;
   packet.set_id(id);
   packet.set_type(headless::CreateHDLeafRequestType);
   packet.set_data(response.SerializeAsString());

   if (!sendData(packet.SerializeAsString(), clientId)) {
      logger_->error("[HeadlessContainerListener::CreateHDLeafResponse] failed to send response CreateHDLeaf packet");
   }
}

void HeadlessContainerListener::CreateEnableTradingResponse(const std::string& clientId, unsigned int id
   , ErrorCode result, const std::string& walletId)
{
   headless::EnableTradingInWalletResponse response;
   response.set_rootwalletid(walletId);
   response.set_errorcode(static_cast<uint32_t>(result));

   headless::RequestPacket packet;
   packet.set_id(id);
   packet.set_type(headless::EnableTradingInWalletType);
   packet.set_data(response.SerializeAsString());

   if (!sendData(packet.SerializeAsString(), clientId)) {
      logger_->error("[HeadlessContainerListener::CreateEnableTradingResponse] failed to send response EnableTradingInWallet packet");
   }
}

void HeadlessContainerListener::CreatePromoteWalletResponse(const std::string& clientId, unsigned int id
   , ErrorCode result, const std::string& walletId)
{
   headless::PromoteWalletToPrimaryResponse response;
   response.set_rootwalletid(walletId);
   response.set_errorcode(static_cast<uint32_t>(result));

   headless::RequestPacket packet;
   packet.set_id(id);
   packet.set_type(headless::EnableTradingInWalletType);
   packet.set_data(response.SerializeAsString());

   if (!sendData(packet.SerializeAsString(), clientId)) {
      logger_->error("[HeadlessContainerListener::CreatePromoteWalletResponse] failed to send response EnableTradingInWallet packet");
   }
}

static SecureBinaryData getPubKey(const std::shared_ptr<bs::core::hd::Leaf> &leaf)
{
   auto rootPtr = leaf->getRootAsset();
   auto rootSingle = std::dynamic_pointer_cast<Armory::Assets::AssetEntry_Single>(rootPtr);
   if (rootSingle == nullptr) {
      return {};
   }
   return rootSingle->getPubKey()->getCompressedKey();
}

#if 0 // settlement is being removed
bool HeadlessContainerListener::onCreateSettlWallet(const std::string &clientId, headless::RequestPacket packet)
{
   headless::CreateSettlWalletRequest request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[{}] failed to parse request", __func__);
      return false;
   }
   const auto priWallet = walletsMgr_->getPrimaryWallet();
   if (!priWallet) {
      logger_->error("[{}] no primary wallet found", __func__);
      packet.set_data("");
      sendData(packet.SerializeAsString(), clientId);
      return false;
   }

   auto authAddr = bs::Address::fromAddressString(request.auth_address());
   const auto settlLeaf = priWallet->getSettlementLeaf(authAddr);
   if (settlLeaf) {
      headless::CreateSettlWalletResponse response;
      response.set_wallet_id(settlLeaf->walletId());
      response.set_public_key(getPubKey(settlLeaf).toBinStr());
      packet.set_data(response.SerializeAsString());
      sendData(packet.SerializeAsString(), clientId);
      return true;
   }

   auto &reqsForAddress = settlLeafReqs_[{clientId, authAddr }];
   reqsForAddress.push_back(packet.id());
   if (reqsForAddress.size() > 1) {
      return true;
   }
   const auto &onPassword = [this, priWallet, clientId, authAddr, id=packet.id()]
      (bs::error::ErrorCode result, const SecureBinaryData &password) {
      headless::CreateSettlWalletResponse response;
      headless::RequestPacket packet;
      packet.set_id(id);
      packet.set_type(headless::CreateSettlWalletType);

      const auto &itReqs = settlLeafReqs_.find({ clientId, authAddr });
      if (itReqs == settlLeafReqs_.end()) {
         logger_->warn("[HeadlessContainerListener] failed to find list of requests");
         packet.set_data(response.SerializeAsString());
         sendData(packet.SerializeAsString(), clientId);
         return;
      }

      const auto &sendAllIds = [this, clientId](const std::string &response
         , const std::vector<uint32_t> &ids)
      {
         if (ids.empty()) {
            return;
         }
         headless::RequestPacket packet;
         packet.set_data(response);
         packet.set_type(headless::CreateSettlWalletType);
         for (const auto &id : ids) {
            packet.set_id(id);
            sendData(packet.SerializeAsString(), clientId);
         }
      };

      if (result != bs::error::ErrorCode::NoError) {
         logger_->warn("[HeadlessContainerListener] password request failed");
         sendAllIds(response.SerializeAsString(), itReqs->second);
         return;
      }

      {
         const bs::core::WalletPasswordScoped lock(priWallet, password);
         const auto leaf = priWallet->createSettlementLeaf(authAddr);
         if (!leaf) {
            logger_->error("[HeadlessContainerListener] failed to create settlement leaf for {}"
               , authAddr.display());
            sendAllIds(response.SerializeAsString(), itReqs->second);
            return;
         }
         response.set_wallet_id(leaf->walletId());
         response.set_public_key(getPubKey(leaf).toBinStr());
      }
      if (callbacks_) {
         callbacks_->walletChanged(response.wallet_id());
      }
      sendAllIds(response.SerializeAsString(), itReqs->second);
      settlLeafReqs_.erase(itReqs);
   };

   Internal::PasswordDialogDataWrapper dialogData = request.passworddialogdata();
   dialogData.insert(PasswordDialogData::WalletId, priWallet->walletId());
   dialogData.insert(PasswordDialogData::AuthAddress, request.auth_address());

   return RequestPasswordIfNeeded(clientId, priWallet->walletId(), {}, headless::CreateSettlWalletType
      , dialogData, onPassword);
}

bool HeadlessContainerListener::onSetSettlementId(const std::string &clientId
   , Blocksettle::Communication::headless::RequestPacket packet)
{
   headless::SetSettlementIdRequest request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[{}] failed to parse request", __func__);
      return false;
   }
   headless::SetSettlementIdResponse response;
   response.set_success(false);

   const auto leaf = walletsMgr_->getWalletById(request.wallet_id());
   const auto settlLeaf = std::dynamic_pointer_cast<bs::core::hd::SettlementLeaf>(leaf);
   if (settlLeaf == nullptr) {
      logger_->error("[{}] no leaf for id {}", __func__, request.wallet_id());
      packet.set_data(response.SerializeAsString());
      sendData(packet.SerializeAsString(), clientId);
      return false;
   }

   // Call addSettlementID only once, otherwise addSettlementID will crash
   const auto settlementId = SecureBinaryData::fromString(request.settlement_id());
   if (settlLeaf->getIndexForSettlementID(settlementId) == UINT32_MAX) {
      settlLeaf->addSettlementID(settlementId);
      settlLeaf->getNewExtAddress();
      if (callbacks_) {
         callbacks_->walletChanged(settlLeaf->walletId());
      }
      logger_->debug("[{}] set settlement id {} for wallet {}", __func__
         , settlementId.toHexStr(), settlLeaf->walletId());
   }

   response.set_success(true);
   response.set_wallet_id(leaf->walletId());

   packet.set_data(response.SerializeAsString());
   return sendData(packet.SerializeAsString(), clientId);
}

bool HeadlessContainerListener::onGetPayinAddr(const std::string &clientId
   , Blocksettle::Communication::headless::RequestPacket packet)
{
   headless::SettlPayinAddressRequest request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[{}] failed to parse request", __func__);
      return false;
   }
   headless::SettlPayinAddressResponse response;
   response.set_success(false);

   const auto wallet = walletsMgr_->getHDWalletById(request.wallet_id());
   if (!wallet) {
      logger_->error("[{}] no hd wallet for id {}", __func__, request.wallet_id());
      packet.set_data(response.SerializeAsString());
      sendData(packet.SerializeAsString(), clientId);
      return false;
   }
   const bs::core::wallet::SettlementData sd { BinaryData::fromString(request.settlement_data().settlement_id())
      , BinaryData::fromString(request.settlement_data().counterparty_pubkey())
      , request.settlement_data().my_pubkey_first() };
   const auto addr = wallet->getSettlementPayinAddress(sd);
   response.set_address(addr.display());
   response.set_wallet_id(wallet->walletId());
   response.set_success(true);

   packet.set_data(response.SerializeAsString());
   return sendData(packet.SerializeAsString(), clientId);
}

bool HeadlessContainerListener::onSettlGetRootPubkey(const std::string &clientId
   , Blocksettle::Communication::headless::RequestPacket packet)
{
   headless::SettlGetRootPubkeyRequest request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[{}] failed to parse request", __func__);
      return false;
   }
   headless::SettlGetRootPubkeyResponse response;
   response.set_success(false);
   if (!request.wallet_id().empty()) {
      const auto leaf = walletsMgr_->getWalletById(request.wallet_id());
      if (!leaf) {
         logger_->error("[{}] no leaf for id {}", __func__, request.wallet_id());
         packet.set_data(response.SerializeAsString());
         sendData(packet.SerializeAsString(), clientId);
         return false;
      }
      response.set_success(true);
      response.set_wallet_id(leaf->walletId());
      response.set_public_key(getPubKey(leaf).toBinStr());
   }
   packet.set_data(response.SerializeAsString());
   return sendData(packet.SerializeAsString(), clientId);
}
#endif   //0

bool HeadlessContainerListener::onGetHDWalletInfo(const std::string &clientId, headless::RequestPacket &packet)
{
   headless::GetHDWalletInfoRequest request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[HeadlessContainerListener] failed to parse GetHDWalletInfoRequest");
      GetHDWalletInfoResponse(clientId, packet.id(), {}, nullptr, "failed to parse request");
      return false;
   }
   const auto &wallet = walletsMgr_->getHDWalletById(request.rootwalletid());
   if (!wallet) {
      logger_->error("[HeadlessContainerListener] failed to find wallet for id {}", request.rootwalletid());
      GetHDWalletInfoResponse(clientId, packet.id(), request.rootwalletid(), nullptr, "failed to find wallet");
      return false;
   }
   GetHDWalletInfoResponse(clientId, packet.id(), request.rootwalletid(), wallet);
   return true;
}

void HeadlessContainerListener::GetHDWalletInfoResponse(const std::string &clientId, unsigned int id
   , const std::string &walletId, const std::shared_ptr<bs::core::hd::Wallet> &wallet, const std::string &error)
{
   headless::GetHDWalletInfoResponse response;
   if (!error.empty()) {
      response.set_error(error);
   }
   if (wallet) {
      for (const auto &encType : wallet->encryptionTypes()) {
         response.add_enctypes(static_cast<uint32_t>(encType));
      }
      for (const auto &encKey : wallet->encryptionKeys()) {
         response.add_enckeys(encKey.toBinStr());
      }
      response.set_rankm(wallet->encryptionRank().m);
      response.set_rankn(wallet->encryptionRank().n);
   }
   if (!walletId.empty()) {
      response.set_rootwalletid(walletId);
   }

   headless::RequestPacket packet;
   packet.set_id(id);
   packet.set_type(headless::GetHDWalletInfoRequestType);
   packet.set_data(response.SerializeAsString());

   if (!sendData(packet.SerializeAsString(), clientId)) {
      logger_->error("[HeadlessContainerListener::{}] failed to send to {}", __func__
         , BinaryData::fromString(clientId).toHexStr());
   }
}

void HeadlessContainerListener::AutoSignActivatedEvent(ErrorCode result
   , const std::string &walletId)
{
   if (callbacks_) {
      callbacks_->autoSignActivated(result == ErrorCode::NoError, walletId);
   }

   headless::AutoSignActEvent autoSignActEvent;
   autoSignActEvent.set_rootwalletid(walletId);
   autoSignActEvent.set_errorcode(static_cast<uint>(result));

   headless::RequestPacket packet;
   packet.set_type(headless::AutoSignActType);
   packet.set_data(autoSignActEvent.SerializeAsString());

   sendData(packet.SerializeAsString());
}

bool HeadlessContainerListener::checkSpendLimit(uint64_t value, const std::string &walletId
   , bool autoSign)
{
   if (autoSign && isAutoSignActive(walletId)) {
      if (value > limits_.autoSignSpendXBT) {
         logger_->warn("[HeadlessContainerListener] requested auto-sign spend {} exceeds limit {}", value
            , limits_.autoSignSpendXBT);
         deactivateAutoSign(walletId, ErrorCode::TxSpendLimitExceed);
         return false;
      }
   }
   else {
      if (value > limits_.manualSpendXBT) {
         logger_->warn("[HeadlessContainerListener] requested manual spend {} exceeds limit {}", value
            , limits_.manualSpendXBT);
         return false;
      }
   }
   return true;
}

void HeadlessContainerListener::sendUpdateStatuses(std::string clientId)
{
   headless::UpdateStatus evt;
   evt.set_status(noWallets_ ? headless::UpdateStatus_WalletsStatus_NoWallets : headless::UpdateStatus_WalletsStatus_Unknown);

   headless::RequestPacket packet;
   packet.set_type(headless::UpdateStatusType);
   packet.set_data(evt.SerializeAsString());

   sendData(packet.SerializeAsString(), clientId);
}

void HeadlessContainerListener::sendSyncWallets(std::string clientId /*= {}*/)
{
   headless::UpdateStatus evt;
   evt.set_status(headless::UpdateStatus_WalletsStatus_ReadyToSync);

   headless::RequestPacket packet;
   packet.set_type(headless::UpdateStatusType);
   packet.set_data(evt.SerializeAsString());

   sendData(packet.SerializeAsString(), clientId);
}

void HeadlessContainerListener::onXbtSpent(int64_t value, bool autoSign)
{
   if (autoSign) {
      limits_.autoSignSpendXBT -= value;
      logger_->debug("[HeadlessContainerListener] new auto-sign spend limit = {} (-{})"
         , limits_.autoSignSpendXBT, value);
   }
   else {
      limits_.manualSpendXBT -= value;
      logger_->debug("[HeadlessContainerListener] new manual spend limit = {} (-{})"
         , limits_.manualSpendXBT, value);
   }
}

bs::error::ErrorCode HeadlessContainerListener::activateAutoSign(const std::string &walletId
   , const SecureBinaryData &password)
{
   logger_->info("Activate AutoSign for {}", walletId);

   const auto &hdWallet = walletId.empty() ? walletsMgr_->getPrimaryWallet() : walletsMgr_->getHDWalletById(walletId);
   if (!hdWallet) {
      AutoSignActivatedEvent(ErrorCode::WalletNotFound, walletId);
      return ErrorCode::WalletNotFound;
   }

   std::string seedStr, privKeyStr;

   try {
      const bs::core::WalletPasswordScoped lock(hdWallet, password);
      const auto &seed = hdWallet->getDecryptedSeed();
      if (seed.empty()) {
         AutoSignActivatedEvent(ErrorCode::MissingPassword, walletId);
         return ErrorCode::MissingPassword;
      }
   }
   catch (...) {
      logger_->error("[HeadlessContainerListener::activateAutoSign] wallet {} decryption error"
         , walletId);
      AutoSignActivatedEvent(ErrorCode::InvalidPassword, walletId);
      return ErrorCode::InvalidPassword;
   }

   passwords_[hdWallet->walletId()] = password;

   // multicast event
   AutoSignActivatedEvent(ErrorCode::NoError, walletId);

   return ErrorCode::NoError;
}

bs::error::ErrorCode HeadlessContainerListener::deactivateAutoSign(const std::string &walletId
   , bs::error::ErrorCode reason)
{
   logger_->info("Deactivate AutoSign for {} (error code: {})", walletId, static_cast<int>(reason));

   if (walletId.empty()) {
      passwords_.clear();
   }
   else {
      passwords_.erase(walletId);
   }

   // multicast event
   AutoSignActivatedEvent(ErrorCode::AutoSignDisabled, walletId);

   return ErrorCode::AutoSignDisabled;
}

bool HeadlessContainerListener::isAutoSignActive(const std::string &walletId) const
{
   if (walletId.empty()) {
      return !passwords_.empty();
   }
   if (passwords_.find(walletId) != passwords_.end()) {
      return true;
   }
   const auto &rootWallet = walletsMgr_->getHDRootForLeaf(walletId);
   if (!rootWallet) {
      return false;
   }
   return (passwords_.find(rootWallet->walletId()) != passwords_.end());
}

void HeadlessContainerListener::walletsListUpdated()
{
   logger_->debug("send WalletsListUpdatedType message");

   headless::RequestPacket packet;
   packet.set_type(headless::WalletsListUpdatedType);
   sendData(packet.SerializeAsString());
}

void HeadlessContainerListener::windowVisibilityChanged(bool visible)
{
   headless::WindowStatus msg;
   msg.set_visible(visible);

   headless::RequestPacket packet;
   packet.set_type(headless::WindowStatusType);
   packet.set_data(msg.SerializeAsString());
   sendData(packet.SerializeAsString());
}

void HeadlessContainerListener::resetConnection(ServerConnection *connection)
{
   logger_->debug("[{}:{}] terminal connection is set {}", __func__, (void*)this
      , (connection != nullptr));
   connection_ = connection;
}

bool HeadlessContainerListener::onSyncWalletInfo(const std::string &clientId, headless::RequestPacket packet)
{
   headless::SyncWalletInfoResponse response = bs::sync::exportHDWalletsInfoToPbMessage(walletsMgr_);

   packet.set_data(response.SerializeAsString());
   return sendData(packet.SerializeAsString(), clientId);
}

bool HeadlessContainerListener::onSyncHDWallet(const std::string &clientId, headless::RequestPacket packet)
{
   headless::SyncWalletRequest request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[{}] failed to parse request", __func__);
      return false;
   }

   headless::SyncHDWalletResponse response;
   const auto hdWallet = walletsMgr_->getHDWalletById(request.walletid());
   if (hdWallet) {
      response.set_walletid(hdWallet->walletId());
      for (const auto &group : hdWallet->getGroups()) {
         auto groupData = response.add_groups();
         groupData->set_type(group->index() | bs::hd::hardFlag);
         groupData->set_ext_only(hdWallet->isExtOnly());

         if (group->index() == bs::hd::CoinType::BlockSettle_Auth) {
            const auto authGroup = std::dynamic_pointer_cast<bs::core::hd::AuthGroup>(group);
            if (authGroup) {
               groupData->set_salt(authGroup->getSalt().toBinStr());
            }
         }

         if (static_cast<bs::hd::CoinType>(group->index()) == bs::hd::CoinType::BlockSettle_Auth) {
            continue;      // don't sync leaves for auth before setUserId is asked
         }
         for (const auto &leaf : group->getAllLeaves()) {
            auto leafData = groupData->add_leaves();
            leafData->set_id(leaf->walletId());
            leafData->set_path(leaf->path().toString());

            if (groupData->type() == bs::hd::CoinType::BlockSettle_Settlement) {
               const auto settlLeaf = std::dynamic_pointer_cast<bs::core::hd::SettlementLeaf>(leaf);
               if (settlLeaf == nullptr) {
                  throw std::runtime_error("unexpected leaf type");
               }
               const auto rootAsset = settlLeaf->getRootAsset();
               const auto rootSingle = std::dynamic_pointer_cast<Armory::Assets::AssetEntry_Single>(rootAsset);
               if (rootSingle == nullptr) {
                  throw std::runtime_error("invalid root asset");
               }
               const auto authAddr = BtcUtils::getHash160(rootSingle->getPubKey()->getCompressedKey());
               leafData->set_extra_data(authAddr.toBinStr());
            }
         }
      }
   } else {
      logger_->error("[{}] failed to find HD wallet with id {}", __func__, request.walletid());
      return false;
   }

   packet.set_data(response.SerializeAsString());
   return sendData(packet.SerializeAsString(), clientId);
}

bool HeadlessContainerListener::onSyncWallet(const std::string &clientId, headless::RequestPacket packet)
{
   headless::SyncWalletRequest request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[{}] failed to parse request", __func__);
      return false;
   }

   const auto wallet = walletsMgr_->getWalletById(request.walletid());
   if (!wallet) {
      logger_->error("[{}] failed to find wallet with id {}", __func__, request.walletid());
      return false;
   }

   const auto &lbdSend = [this, wallet, id=packet.id(), clientId]
   {
      headless::SyncWalletResponse response = bs::sync::exportHDLeafToPbMessage(wallet);

      headless::RequestPacket packet;
      packet.set_id(id);
      packet.set_data(response.SerializeAsString());
      packet.set_type(headless::SyncWalletType);
      sendData(packet.SerializeAsString(), clientId);
   };
   lbdSend();
   return true;
}

bool HeadlessContainerListener::onSyncComment(const std::string &clientId, headless::RequestPacket packet)
{
   headless::SyncCommentRequest request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[{}] failed to parse request", __func__);
      return false;
   }
   const auto wallet = walletsMgr_->getWalletById(request.walletid());
   if (!wallet) {
      logger_->error("[{}] failed to find wallet with id {}", __func__, request.walletid());
      return false;
   }
   bool rc = false;
   if (!request.address().empty()) {
      auto addrObj = bs::Address::fromAddressString(request.address());
      rc = wallet->setAddressComment(addrObj, request.comment());
      logger_->debug("[{}] comment for address {} is set: {}", __func__, request.address(), rc);
   }
   else {
      rc = wallet->setTransactionComment(BinaryData::fromString(request.txhash()), request.comment());
      logger_->debug("[{}] comment for TX {} is set: {}", __func__, BinaryData::fromString(request.txhash()).toHexStr(true), rc);
   }
   return rc;
}

void HeadlessContainerListener::SyncAddrsResponse(const std::string &clientId
   , unsigned int id, const std::string &walletId, bs::sync::SyncState state)
{
   headless::SyncAddressesResponse response;
   response.set_wallet_id(walletId);
   headless::SyncState respState = headless::SyncState_Failure;
   switch (state) {
   case bs::sync::SyncState::Success:
      respState = headless::SyncState_Success;
      break;
   case bs::sync::SyncState::NothingToDo:
      respState = headless::SyncState_NothingToDo;
      break;
   case bs::sync::SyncState::Failure:
      respState = headless::SyncState_Failure;
      break;
   }
   response.set_state(respState);

   headless::RequestPacket packet;
   packet.set_id(id);
   packet.set_data(response.SerializeAsString());
   packet.set_type(headless::SyncAddressesType);
   sendData(packet.SerializeAsString(), clientId);
}

bool HeadlessContainerListener::onSyncAddresses(const std::string &clientId, headless::RequestPacket packet)
{
   headless::SyncAddressesRequest request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[{}] failed to parse request", __func__);
      return false;
   }
   const auto wallet = walletsMgr_->getWalletById(request.wallet_id());
   if (wallet == nullptr) {
      SyncAddrsResponse(clientId, packet.id(), request.wallet_id(), bs::sync::SyncState::Failure);
      logger_->error("[{}] wallet with ID {} not found", __func__, request.wallet_id());
      return false;
   }

   std::set<BinaryData> addrSet;
   for (int i = 0; i < request.addresses_size(); ++i) {
      addrSet.insert(BinaryData::fromString(request.addresses(i)));
   }

   //resolve the path and address type for addrSet
   std::map<BinaryData, bs::hd::Path> parsedMap;
   try {
      parsedMap = std::move(wallet->indexPath(addrSet));
   } catch (Armory::Accounts::AccountException &e) {
      //failure to find even one of the addresses means the wallet chain needs
      //extended further
      SyncAddrsResponse(clientId, packet.id(), request.wallet_id(), bs::sync::SyncState::Failure);
      logger_->error("[{}] failed to find indices for {} addresses in {}: {}"
         , __func__, addrSet.size(), request.wallet_id(), e.what());
      return false;
   }

   std::map<bs::hd::Path::Elem, std::set<bs::hd::Path>> mapByPath;
   for (auto& parsedPair : parsedMap) {
      auto elem = parsedPair.second.get(-2);
      auto& mapping = mapByPath[elem];
      mapping.insert(parsedPair.second);
   }

   //request each chain for the relevant address types
   bool update = false;
   try {
      for (auto& mapping : mapByPath) {
         for (auto& path : mapping.second) {
            auto resultPair = wallet->synchronizeUsedAddressChain(path.toString());
            update |= resultPair.second;
         }
      }
   }
   catch (const std::exception &e) {
      logger_->error("[{}] failed to sync address[es] in {}: {}", __func__, wallet->walletId(), e.what());
      return false;
   }

   if (update) {
      if (callbacks_) {
         callbacks_->walletChanged(wallet->walletId());
      }
      SyncAddrsResponse(clientId, packet.id(), request.wallet_id(), bs::sync::SyncState::Success);
   }
   else {
      SyncAddrsResponse(clientId, packet.id(), request.wallet_id(), bs::sync::SyncState::NothingToDo);
   }
   return true;
}

bool HeadlessContainerListener::onExtAddrChain(const std::string &clientId, headless::RequestPacket packet)
{
   headless::ExtendAddressChainRequest request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[{}] failed to parse request", __func__);
      return false;
   }
   const auto wallet = walletsMgr_->getWalletById(request.wallet_id());
   if (wallet == nullptr) {
      logger_->error("[{}] wallet with ID {} not found", __func__, request.wallet_id());
      return false;
   }

   const auto &lbdSend = [this, wallet, request, id=packet.id(), clientId] {
      headless::ExtendAddressChainResponse response;
      response.set_wallet_id(wallet->walletId());

      try {
         auto&& newAddrVec = wallet->extendAddressChain(request.count(), request.ext_int());

         if (callbacks_) {
            callbacks_->walletChanged(wallet->walletId());
         }

         for (const auto &addr : newAddrVec) {
            auto &&index = wallet->getAddressIndex(addr);
            auto addrData = response.add_addresses();
            addrData->set_address(addr.display());
            addrData->set_index(index);
         }
      }
      catch (const std::exception &e) {
         logger_->error("[HeadlessContainerListener::onExtAddrChain] failed: {}", e.what());
      }

      headless::RequestPacket packet;
      packet.set_id(id);
      packet.set_type(headless::ExtendAddressChainType);
      packet.set_data(response.SerializeAsString());
      sendData(packet.SerializeAsString(), clientId);
   };
   lbdSend();
   return true;
}

bool HeadlessContainerListener::onSyncNewAddr(const std::string &clientId, headless::RequestPacket packet)
{
   headless::SyncNewAddressRequest request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[{}] failed to parse request", __func__);
      return false;
   }
   const auto wallet = walletsMgr_->getWalletById(request.wallet_id());
   if (wallet == nullptr) {
      logger_->error("[{}] wallet with ID {} not found", __func__, request.wallet_id());
      return false;
   }

   headless::ExtendAddressChainResponse response;
   response.set_wallet_id(wallet->walletId());

   for (int i = 0; i < request.addresses_size(); ++i) {
      const auto inData = request.addresses(i);
      auto outData = response.add_addresses();
      const auto addr = wallet->synchronizeUsedAddressChain(inData.index()).first.display();
      outData->set_address(addr);
      outData->set_index(inData.index());
   }

   if (callbacks_) {
      callbacks_->walletChanged(wallet->walletId());
   }

   packet.set_data(response.SerializeAsString());
   sendData(packet.SerializeAsString(), clientId);
   return true;
}

#if 0 // chat and settlement are being removed
bool HeadlessContainerListener::onChatNodeRequest(const std::string &clientId, headless::RequestPacket packet)
{
   headless::ChatNodeRequest request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[{}] failed to parse request", __func__);
      return false;
   }
   headless::ChatNodeResponse response;
   const auto hdWallet = walletsMgr_->getHDWalletById(request.wallet_id());
   if (hdWallet) {
      response.set_wallet_id(hdWallet->walletId());
      const auto chatNode = hdWallet->getChatNode();
      if (!chatNode.getPrivateKey().empty()) {
         response.set_b58_chat_node(chatNode.getBase58().toBinStr());
      }
   }
   else {
      logger_->error("[{}] HD wallet with id {} not found", __func__, request.wallet_id());
   }

   packet.set_data(response.SerializeAsString());
   sendData(packet.SerializeAsString(), clientId);
   return true;
}

bool HeadlessContainerListener::onSettlAuthRequest(const std::string &clientId, headless::RequestPacket packet)
{
   headless::SettlementAuthAddress request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[{}] failed to parse request", __func__);
      return false;
   }
   std::shared_ptr<bs::core::hd::Leaf> authLeaf;
   const auto &priWallet = walletsMgr_->getPrimaryWallet();
   if (priWallet) {
      const auto &authGroup = priWallet->getGroup(bs::hd::BlockSettle_Auth);
      if (authGroup) {
         const bs::hd::Path authPath({ bs::hd::Purpose::Native, bs::hd::BlockSettle_Auth, 0 });
         authLeaf = authGroup->getLeafByPath(authPath);
      }
   }
   if (authLeaf) {
      if (request.auth_address().empty()) {
         const auto addr = authLeaf->getSettlAuthAddr(BinaryData::fromString(request.settlement_id()));
         request.set_auth_address(addr.display());
         request.set_wallet_id(authLeaf->walletId());
      }
      else {
         const auto settlementId = BinaryData::fromString(request.settlement_id());
         const auto addr = bs::Address::fromAddressString(request.auth_address());
         logger_->debug("[{}] saving {} = {}", __func__, settlementId.toHexStr(), addr.display());
         authLeaf->setSettlementMeta(settlementId, addr);
         return true;
      }
   }
   else {
      logger_->warn("[{}] failed to find auth leaf", __func__);
      request.clear_wallet_id();
   }
   packet.set_data(request.SerializeAsString());
   sendData(packet.SerializeAsString(), clientId);
   return true;
}

bool HeadlessContainerListener::onSettlCPRequest(const std::string &clientId, headless::RequestPacket packet)
{
   headless::SettlementCounterparty request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[{}] failed to parse request", __func__);
      return false;
   }
   std::shared_ptr<bs::core::hd::Leaf> authLeaf;
   const auto &priWallet = walletsMgr_->getPrimaryWallet();
   if (priWallet) {
      const auto &authGroup = priWallet->getGroup(bs::hd::BlockSettle_Auth);
      if (authGroup) {
         const bs::hd::Path authPath({ bs::hd::Purpose::Native, bs::hd::BlockSettle_Auth, 0 });
         authLeaf = authGroup->getLeafByPath(authPath);
      }
   }
   if (authLeaf) {
      if (request.settlement_id().empty() && request.cp_public_key().empty()) {
         const auto keys = authLeaf->getSettlCP(BinaryData::fromString(request.payin_hash()));
         request.set_settlement_id(keys.first.toBinStr());
         request.set_cp_public_key(keys.second.toBinStr());
         request.set_wallet_id(authLeaf->walletId());
      } else {
         const auto payinHash = BinaryData::fromString(request.payin_hash());
         const auto settlementId = BinaryData::fromString(request.settlement_id());
         const auto cpPubKey = BinaryData::fromString(request.cp_public_key());
         logger_->debug("[{}] saving {} = ({}, {})", __func__, payinHash.toHexStr(true)
            , settlementId.toHexStr(), cpPubKey.toHexStr());
         authLeaf->setSettlCPMeta(payinHash, settlementId, cpPubKey);
         return true;
      }
   } else {
      logger_->warn("[{}] failed to find auth leaf", __func__);
      request.clear_wallet_id();
   }
   packet.set_data(request.SerializeAsString());
   sendData(packet.SerializeAsString(), clientId);
   return true;
}
#endif   //0

bool HeadlessContainerListener::onExecCustomDialog(const std::string &clientId, headless::RequestPacket packet)
{
   headless::CustomDialogRequest request;
   if (!request.ParseFromString(packet.data())) {
      logger_->error("[HeadlessContainerListener] failed to parse CustomDialogRequest");
      return false;
   }

   if (callbacks_) {
      callbacks_->customDialog(request.dialogname(), request.variantdata());
   }
   return true;
}

void HeadlessContainerListener::setNoWallets(bool noWallets)
{
   if (noWallets_ != noWallets) {
      noWallets_ = noWallets;
      sendUpdateStatuses();
   }
}

void HeadlessContainerListener::syncWallet()
{
   sendSyncWallets();
}

bool PasswordRequest::operator <(const PasswordRequest &other) const
{
   return dialogExpirationTime < other.dialogExpirationTime;
}
