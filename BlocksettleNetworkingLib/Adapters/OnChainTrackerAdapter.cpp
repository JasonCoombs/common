/*

***********************************************************************************
* Copyright (C) 2020 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "OnChainTrackerAdapter.h"
#include <spdlog/spdlog.h>
#include "ColoredCoinServer.h"

#include "common.pb.h"

using namespace BlockSettle::Common;
using namespace bs::message;

class AddrVerificatorCallbacks : public AuthValidatorCallbacks
{
   friend class OnChainTrackerAdapter;
public:
   explicit AddrVerificatorCallbacks(OnChainTrackerAdapter* parent)
      : parent_(parent)
   {
      walletId_ = "auth_" + CryptoPRNG::generateRandom(8).toHexStr();
   }

   unsigned int topBlock() const override { return parent_->topBlock_; }

   void pushZC(const BinaryData& tx) override
   {
      ArmoryMessage msg;
      auto msgReq = msg.mutable_tx_push();
      msgReq->set_push_id(walletId_);
      auto msgTx = msgReq->add_txs_to_push();
      msgTx->set_tx(tx.toBinStr());
      Envelope env{ parent_->user_, parent_->userBlockchain_, msg.SerializeAsString() };
      parent_->pushFill(env);
   }

   std::string registerAddresses(const std::vector<bs::Address>& addrs) override
   {
      ArmoryMessage msg;
      auto msgReq = msg.mutable_register_wallet();
      msgReq->set_wallet_id(walletId_);
      msgReq->set_as_new(true);
      for (const auto& addr : addrs) {
         msgReq->add_addresses(addr.display());
      }
      Envelope env{ parent_->user_, parent_->userBlockchain_, msg.SerializeAsString() };
      parent_->pushFill(env);
      return std::to_string(env.id());
   }

   void getOutpointsForAddresses(const std::vector<bs::Address>& addrs
      , const OutpointsCb& cb, unsigned int topBlock = 0, unsigned int zcIndex = 0) override
   {
      ArmoryMessage msg;
      auto msgReq = msg.mutable_get_out_points();
      for (const auto& addr : addrs) {
         msgReq->add_addresses(addr.display());
      }
      msgReq->set_height(topBlock);
      msgReq->set_zc_index(zcIndex);

      Envelope env{ parent_->user_, parent_->userBlockchain_, msg.SerializeAsString() };
      parent_->pushFill(env);
      parent_->outpointCallbacks_[env.id()] = cb;
   }
   
   void getSpendableTxOuts(const UTXOsCb& cb) override
   {
      ArmoryMessage msg;
      auto msgReq = msg.mutable_get_spendable_utxos();
      msgReq->add_wallet_ids(walletId_);
      Envelope env{ parent_->user_, parent_->userBlockchain_, msg.SerializeAsString() };
      parent_->pushFill(env);
      parent_->utxoCallbacks_[env.id()] = cb;
   }

   void getUTXOsForAddress(const bs::Address& addr, const UTXOsCb& cb
      , bool withZC = false) override
   {
      ArmoryMessage msg;
      auto msgReq = msg.mutable_get_utxos_for_addr();
      msgReq->set_address(addr.display());
      msgReq->set_with_zc(withZC);
      Envelope env{ parent_->user_, parent_->userBlockchain_, msg.SerializeAsString() };
      parent_->pushFill(env);
      parent_->utxoCallbacks_[env.id()] = cb;
   }

private:
   OnChainTrackerAdapter* parent_{ nullptr };
   std::string walletId_;
};


OnChainTrackerAdapter::OnChainTrackerAdapter(const std::shared_ptr<spdlog::logger> &logger
   , const std::shared_ptr<bs::message::User> &user
   , const std::shared_ptr<bs::message::User> &userBlockchain
   , const std::shared_ptr<bs::message::User>& userWallet
   , const std::shared_ptr<OnChainExternalPlug> &extPlug)
   : logger_(logger), user_(user), userBlockchain_(userBlockchain)
   , userWallet_(userWallet), extPlug_(extPlug)
{
   extPlug->setParent(this, user_);
}

OnChainTrackerAdapter::~OnChainTrackerAdapter()
{
   stop();
}

bool OnChainTrackerAdapter::processEnvelope(const bs::message::Envelope &env)
{
   if (extPlug_->tryProcess(env)) {
      return true;
   }
   else if (env.sender->value() == userBlockchain_->value()) {
      ArmoryMessage msg;
      if (!msg.ParseFromString(env.message)) {
         logger_->error("[{}] failed to parse armory msg #{}", __func__, env.id());
         return true;
      }
      switch (msg.data_case()) {
      case ArmoryMessage::kStateChanged:
         return processArmoryState(msg.state_changed());
      case ArmoryMessage::kNewBlock:
         return processNewBlock(msg.new_block().top_block());
      case ArmoryMessage::kWalletRegistered:
         return processWalletRegistered(env.responseId, msg.wallet_registered());
      case ArmoryMessage::kOutPoints:
         return processOutpoints(env.responseId, msg.out_points());
      case ArmoryMessage::kUtxos:
         return processUTXOs(env.responseId, msg.utxos());
      default: break;
      }
   }
   else if (env.sender->value() == userWallet_->value()) {
      WalletsMessage msg;
      if (!msg.ParseFromString(env.message)) {
         logger_->error("[{}] failed to parse wallets msg #{}", __func__, env.id());
         return true;
      }
      switch (msg.data_case()) {
         case WalletsMessage::kAuthWallet:
            return processAuthWallet(msg.auth_wallet());
         default: break;
      }
   }
   else if (!env.responseId && (env.receiver->value() == user_->value())) {
      OnChainTrackMessage msg;
      if (!msg.ParseFromString(env.message)) {
         logger_->error("[{}] failed to parse own msg #{}", __func__, env.id());
         return true;
      }
      switch (msg.data_case()) {
      case OnChainTrackMessage::kSetAuthAddresses:
         return processAuthAddresses(msg.set_auth_addresses());
      case OnChainTrackMessage::kGetVerifiedAuthAddresses:
         sendVerifiedAuthAddresses();
         break;
      default: break;
      }
   }
   return true;
}

void OnChainTrackerAdapter::onStart()
{
   ccTracker_ = std::make_shared<CcTrackerClient>(logger_);
   authCallbacks_ = std::make_shared<AddrVerificatorCallbacks>(this);
   extPlug_->sendAuthValidationListRequest();

   OnChainTrackMessage msg;
   msg.mutable_loading();
   Envelope envBC{ user_, nullptr, msg.SerializeAsString()
      , (SeqId)EnvelopeFlags::GlobalBroadcast };
   pushFill(envBC);
}

void OnChainTrackerAdapter::connectAuthVerificator()
{
   if (!authVerificator_ || !blockchainReady_) {
      return;
   }
   const auto& onlineResultCb = [this](bool result)
   {
      if (result) {
         logger_->debug("[connectAuthVerificator] auth is online");
         authOnline_ = true;
         authAddressVerification();
      }
      else {
         logger_->error("[OnChainTrackerAdapter::connectAuthVerificator] failed to go online");
      }
   };
   if (!authVerificator_->goOnline(onlineResultCb)) {
      logger_->error("[OnChainTrackerAdapter::connectAuthVerificator] goOnline failed");
   }
}

void OnChainTrackerAdapter::authAddressVerification()
{
   std::lock_guard<std::recursive_mutex> lock(mutex_);
   if (!authVerificator_ || !authOnline_ || userAddresses_.empty()) {
      logger_->warn("[{}] not ready: {} {} {}", (authVerificator_ != nullptr)
         , authOnline_, userAddresses_.empty());
      return;
   }

   const auto& cbOPs = [this](const OutpointBatch& batch)
   {
      if (authVerificator_->update(batch) == UINT32_MAX) {
         logger_->warn("[OnChainTrackerAdapter::authAddressVerification] update failed");
         return;
      }
      for (const auto& addr : userAddresses_) {
         const auto& cbAddrOPs = [this, addr](const OutpointBatch& opBatch)
         {
            try {
               const auto& state = AuthAddressLogic::getAuthAddrState(*authVerificator_
                  , opBatch);
               completeAuthVerification(addr, state);
            } catch (const AuthLogicException& e) {
               logger_->error("[OnChainTrackerAdapter::authAddressVerification] failed "
                  "to validate state for {}: {}", addr.display(), e.what());
               completeAuthVerification(addr, AddressVerificationState::VerificationFailed);
            } catch (const std::exception& e) {
               logger_->error("[OnChainTrackerAdapter::authAddressVerification] auth state"
                  " for {} validation error: {}", addr.display(), e.what());
            }
         };
         authVerificator_->getOutpointsFor(addr, cbAddrOPs);
      }
   };
   authVerificator_->getValidationOutpointsBatch(cbOPs);
}

void OnChainTrackerAdapter::completeAuthVerification(const bs::Address& addr
   , AddressVerificationState state)
{
   const auto& it = addrStates_.find(addr);
   if (it != addrStates_.end()) {
      if (it->second == state) {
         return;
      }
   }
   addrStates_[addr] = state;
   OnChainTrackMessage msg;
   auto msgState = msg.mutable_auth_state();
   msgState->set_address(addr.display());
   msgState->set_state((int)state);
   Envelope env{ user_, nullptr, msg.SerializeAsString()
      , (SeqId)EnvelopeFlags::GlobalBroadcast };
   pushFill(env);
   sendVerifiedAuthAddresses();
}

bool OnChainTrackerAdapter::processArmoryState(const ArmoryMessage_State& state)
{
   topBlock_ = state.top_block();
   switch (static_cast<ArmoryState>(state.state())) {
   case ArmoryState::Ready:
      if (blockchainReady_) {
         break;
      }
      blockchainReady_ = true;
      connectAuthVerificator();
      break;
   default:
      blockchainReady_ = false;
      break;
   }
   return true;
}

bool OnChainTrackerAdapter::processNewBlock(uint32_t topBlock)
{
   topBlock_ = topBlock;
   authAddressVerification();
   return true;
}

bool OnChainTrackerAdapter::processWalletRegistered(bs::message::SeqId msgId
   , const ArmoryMessage_WalletRegistered& response)
{
   authVerificator_->pushRefreshID({ BinaryData::fromString(std::to_string(msgId)) });
   return true;
}

bool OnChainTrackerAdapter::processOutpoints(bs::message::SeqId msgId
   , const ArmoryMessage_OutpointsForAddrList& response)
{
   const auto& it = outpointCallbacks_.find(msgId);
   if (it == outpointCallbacks_.end()) {
      logger_->error("[{}] unknown response #{}", msgId);
      return true;
   }
   OutpointBatch batch;
   batch.heightCutoff_ = response.height_cutoff();
   batch.zcIndexCutoff_ = response.zc_index_cutoff();
   for (const auto& opData : response.outpoints()) {
      auto& outpoints = batch.outpoints_[BinaryData::fromString(opData.id())];
      for (const auto& op : opData.outpoints()) {
         outpoints.push_back({ BinaryData::fromString(op.hash()), op.index()
            , op.tx_height(), op.tx_index(), op.value(), op.spent()
            , BinaryData::fromString(op.spender_hash()) });
      }
   }
   it->second(std::move(batch));
   outpointCallbacks_.erase(it);
   return true;
}

bool OnChainTrackerAdapter::processUTXOs(bs::message::SeqId msgId
   , const ArmoryMessage_UTXOs& response)
{
   const auto& it = utxoCallbacks_.find(msgId);
   if (it == utxoCallbacks_.end()) {
      return false;  // wait some more time
   }
   std::vector<UTXO> utxos;
   utxos.reserve(response.utxos_size());
   for (const auto& utxoData : response.utxos()) {
      UTXO utxo;
      utxo.unserialize(BinaryData::fromString(utxoData));
      utxos.push_back(std::move(utxo));
   }
   it->second(utxos);
   utxoCallbacks_.erase(it);
   return true;
}

bool OnChainTrackerAdapter::processAuthWallet(const WalletsMessage_WalletData& authWallet)
{
   OnChainTrackMessage_AuthAddresses msg;
   msg.set_wallet_id(authWallet.wallet_id());
   for (const auto& addr : authWallet.used_addresses()) {
      msg.add_addresses(addr.address());
   }
   return processAuthAddresses(msg);
}

bool OnChainTrackerAdapter::processAuthAddresses(const OnChainTrackMessage_AuthAddresses& request)
{
   logger_->debug("[{}] adding {} auth addresses from {}", __func__
      , request.addresses_size(), request.wallet_id());
   std::lock_guard<std::recursive_mutex> lock(mutex_);
   for (const auto& addr : request.addresses()) {
      try {
         userAddresses_.insert(bs::Address::fromAddressString(addr));
      }
      catch (const std::exception& e) {
         logger_->error("[{}] failed to decode user address: {}", __func__, e.what());
      }
   }
   authAddressVerification();
   return true;
}

void OnChainTrackerAdapter::sendVerifiedAuthAddresses()
{
   OnChainTrackMessage msg;
   auto msgVerified = msg.mutable_verified_auth_addresses();
   for (const auto& state : addrStates_) {
      if (state.second == AddressVerificationState::Verified) {
         msgVerified->add_addresses(state.first.display());
      }
   }
   Envelope env{ user_, nullptr, msg.SerializeAsString()
      , (SeqId)EnvelopeFlags::GlobalBroadcast };
   pushFill(env);
}

void OnChainTrackerAdapter::onAuthValidationAddresses(const std::vector<std::string>& addrs)
{
   std::lock_guard<std::recursive_mutex> lock(mutex_);
   authVerificator_ = std::make_unique<AuthAddressValidator>(authCallbacks_);
   for (const auto& addr : addrs) {
      try {
         authVerificator_->addValidationAddress(bs::Address::fromAddressString(addr));
      }
      catch (const std::exception& e) {
         logger_->error("[{}] invalid BS validation address: {}", __func__, e.what());
      }
   }
   connectAuthVerificator();
}
