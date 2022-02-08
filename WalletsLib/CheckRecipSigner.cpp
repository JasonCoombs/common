/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "CheckRecipSigner.h"

#include "ArmoryConnection.h"
#include "BitcoinSettings.h"
#include "CoinSelection.h"

using namespace bs;
using namespace Armory::Signer;


void bs::TxAddressChecker::containsInputAddress(Tx tx, std::function<void(bool)> cb
   , uint64_t lotsize, uint64_t value, unsigned int inputId)
{
   if (!tx.isInitialized()) {
      cb(false);
      return;
   }
   TxIn in = tx.getTxInCopy(inputId);
   if (!in.isInitialized() || !lotsize) {
      cb(false);
      return;
   }
   OutPoint op = in.getOutPoint();

   const auto &cbTX = [this, op, cb, lotsize, value](const Tx &prevTx) {
      if (!prevTx.isInitialized()) {
         cb(false);
         return;
      }
      const TxOut prevOut = prevTx.getTxOutCopy(op.getTxOutIndex());
      const auto txAddr = bs::Address::fromTxOut(prevOut);
      const auto prevOutVal = prevOut.getValue();
      if ((txAddr.prefixed() == address_.prefixed()) && (value <= prevOutVal)) {
         cb(true);
         return;
      }
      if ((txAddr.getType() != AddressEntryType_P2PKH) && ((prevOutVal % lotsize) == 0)) {
         containsInputAddress(prevTx, cb, lotsize, prevOutVal);
      }
      else {
         cb(false);
      }
   };

   if (!armory_) {
      cb(false);
      return;
   }

   if (!armory_->getTxByHash(op.getTxHash(), cbTX, true)) {
      cb(false);
   }
}


bool CheckRecipSigner::findRecipAddress(const Address &address, cbFindRecip cb) const
{
   uint64_t valOutput = 0, valReturn = 0, valInput = 0;
   for (const auto& group : recipients_) {
      for (const auto& recipient : group.second) {
         const auto recipientAddress = bs::CheckRecipSigner::getRecipientAddress(recipient);
         if (address == recipientAddress) {
            valOutput += recipient->getValue();
         }
         else {
            valReturn += recipient->getValue();
         }
      }
   }
   for (const auto &spender : spenders_) {
      valInput += spender->getValue();
   }
   if (valOutput) {
      if (cb) {
         cb(valOutput, valReturn, valInput);
      }
      return true;
   }
   return false;
}

struct recip_compare {
   bool operator() (const std::shared_ptr<ScriptRecipient> &a, const std::shared_ptr<ScriptRecipient> &b) const
   {
      return (a->getSerializedScript() < b->getSerializedScript());
   }
};

void CheckRecipSigner::hasInputAddress(const bs::Address &addr, std::function<void(bool)> cb, uint64_t lotsize)
{
   if (!armory_) {
      cb(false);
      return;
   }
   for (const auto &spender : spenders_) {
      auto outpoint = spender->getOutpoint();
      BinaryRefReader brr(outpoint);
      auto&& hash = brr.get_BinaryDataRef(32);
      txHashSet_.insert(hash);
   }
   auto checker = std::make_shared<TxAddressChecker>(addr, armory_);
   resultFound_ = false;

   const auto &cbTXs = [this, cb, checker, lotsize, handle = validityFlag_.handle()]
      (const AsyncClient::TxBatchResult &txs, std::exception_ptr exPtr) mutable
   {
      ValidityGuard lock(handle);
      if (!handle.isValid()) {
         return;
      }
      if (exPtr != nullptr) {
         cb(false);
         return;
      }
      for (const auto &tx : txs) {
         const auto &cbContains = [this, cb, txHash=tx.first, checker, handle]
            (bool contains) mutable
         {
            ValidityGuard lock(handle);
            if (!handle.isValid()) {
               return;
            }
            if (resultFound_) {
               return;
            }
            txHashSet_.erase(txHash);
            if (contains) {
               resultFound_ = true;
               cb(true);
               return;
            }
            if (txHashSet_.empty()) {
               resultFound_ = true;
               cb(false);
            }
         };
         if (!resultFound_) {
            if (tx.second) {
               checker->containsInputAddress(*tx.second, cbContains, lotsize);
            }
            else {
               cb(false);
               continue;
            }
         }
      }
   };
   if (txHashSet_.empty()) {
      cb(false);
   }
   else {
      armory_->getTXsByHash(txHashSet_, cbTXs, true);
   }
}

bool CheckRecipSigner::hasReceiver() const
{
   return !recipients_.empty();
}

uint64_t CheckRecipSigner::estimateFee(float &feePerByte, uint64_t fixedFee) const
{
   size_t txSize = 0;
   std::vector<UTXO> inputs;
   inputs.reserve(spenders_.size());
   for (const auto &spender : spenders_) {
      inputs.emplace_back(std::move(spender->getUtxo()));
   }
   auto transactions = bs::Address::decorateUTXOsCopy(inputs);

   const auto origFeePerByte = feePerByte;
   try {
      Armory::CoinSelection::PaymentStruct payment(recipients_, fixedFee, 0, 0);

      auto usedUTXOCopy{ transactions };
      Armory::CoinSelection::UtxoSelection selection{ usedUTXOCopy };
      selection.computeSizeAndFee(payment);

      if (selection.fee_byte_ > 0) {
         feePerByte = selection.fee_byte_;
      }

      const size_t nonWitSize = selection.size_ - selection.witnessSize_;
      txSize = std::ceil(static_cast<float>(3 * nonWitSize + selection.size_) / 4.0f);
   } catch (...) {}
   return txSize * ((origFeePerByte > 0) ? origFeePerByte : feePerByte);
}

bool CheckRecipSigner::isRBF() const
{
   for (const auto &spender : spenders()) {
      if (spender->getSequence() < (UINT32_MAX - 1)) {
         return true;
      }
   }
   return false;
}

bool CheckRecipSigner::GetInputAddressList(const std::shared_ptr<spdlog::logger> &logger
   , std::function<void(std::vector<bs::Address>)> cb)
{
   auto result = std::make_shared<std::vector<Address>>();

   if (!armory_) {
      logger->error("[CheckRecipSigner::GetInputAddressList] there is no armory connection");
      return false;
   }

   const auto &cbTXs = [this, result, cb, handle = validityFlag_.handle()]
      (const AsyncClient::TxBatchResult &txs, std::exception_ptr exPtr) mutable
   {
      ValidityGuard lock(handle);
      if (!handle.isValid()) {
         return;
      }
      if (exPtr != nullptr) {
         if (cb) {
            cb({});
         }
         return;
      }
      for (const auto &tx : txs) {
         if (!result) {
            return;
         }
         txHashSet_.erase(tx.first);
         if (tx.second) {
            for (const auto &txOutIdx : txOutIdx_[tx.first]) {
               const TxOut prevOut = tx.second->getTxOutCopy(txOutIdx);
               result->emplace_back(bs::Address::fromHash(prevOut.getScrAddressStr()));
            }
         }
         if (txHashSet_.empty()) {
            txOutIdx_.clear();
            cb(*result.get());
         }
      }
   };
   const auto &cbOutputTXs = [this, cbTXs, cb, handle = validityFlag_.handle()]
      (const AsyncClient::TxBatchResult &txs, std::exception_ptr exPtr) mutable
   {
      ValidityGuard lock(handle);
      if (!handle.isValid()) {
         return;
      }
      if (exPtr != nullptr) {
         cb({});
         return;
      }
      for (const auto &tx : txs) {
         if (!tx.second) {
            continue;
         }
         for (size_t i = 0; i < tx.second->getNumTxIn(); ++i) {
            TxIn in = tx.second->getTxInCopy((int)i);
            OutPoint op = in.getOutPoint();
            txHashSet_.insert(op.getTxHash());
            txOutIdx_[op.getTxHash()].insert(op.getTxOutIndex());
         }
      }
      if (txHashSet_.empty()) {
         cb({});
      }
      else {
         armory_->getTXsByHash(txHashSet_, cbTXs, true);
      }
   };

   std::set<BinaryData> outputHashSet;
   txHashSet_.clear();
   for (const auto &spender : spenders_) {
      auto outputHash = spender->getOutputHash();
      if (outputHash.empty() || outputHash.getSize() == 0) {
         logger->warn("[CheckRecipSigner::GetInputAddressList] spender has empty output hash");
      }
      else {
         outputHashSet.emplace(std::move(outputHash));
      }
   }
   if (outputHashSet.empty()) {
      cb({});
      return false;
   }
   else {
      armory_->getTXsByHash(outputHashSet, cbOutputTXs, true);
   }
   return true;
}

bool CheckRecipSigner::verifyPartial(void)
{
   //TODO: this isnt a sig check at all, fix it
   for (const auto &spender : spenders_) {
      if (spender->isResolved()) {
         return true;
      }
   }
   return false;
}

void CheckRecipSigner::reset()
{
   spenders_.clear();
   recipients_.clear();
}

int TxChecker::receiverIndex(const bs::Address &addr) const
{
   if (!tx_.isInitialized()) {
      return -1;
   }

   for (size_t i = 0; i < tx_.getNumTxOut(); i++) {
      TxOut out = tx_.getTxOutCopy((int)i);
      if (!out.isInitialized()) {
         continue;
      }
      const auto &txAddr = bs::Address::fromTxOut(out);
      if (addr.id() == txAddr.id()) {
         return (int)i;
      }
   }
   return -1;
}

bool TxChecker::hasReceiver(const bs::Address &addr) const
{
   return (receiverIndex(addr) >= 0);
}

void TxChecker::hasSpender(const bs::Address &addr, const std::shared_ptr<ArmoryConnection> &armory
   , std::function<void(bool)> cb) const
{
   if (!tx_.isInitialized()) {
      cb(false);
      return;
   }

   struct Result {
      std::set<BinaryData> txHashSet;
      std::map<BinaryData, std::unordered_set<uint32_t>> txOutIdx;
   };
   auto result = std::make_shared<Result>();

   const auto &cbTXs = [result, addr, cb]
      (const AsyncClient::TxBatchResult &txs, std::exception_ptr)
   {
      for (const auto &tx : txs) {
         if (!tx.second) {
            continue;
         }
         for (const auto &txOutIdx : result->txOutIdx[tx.first]) {
            const TxOut prevOut = tx.second->getTxOutCopy(txOutIdx);
            const bs::Address &txAddr = bs::Address::fromTxOut(prevOut);
            if (txAddr.id() == addr.id()) {
                cb(true);
                return;
            }
         }
      }
      cb(false);
   };

   for (size_t i = 0; i < tx_.getNumTxIn(); ++i) {
      TxIn in = tx_.getTxInCopy((int)i);
      if (!in.isInitialized()) {
         continue;
      }
      OutPoint op = in.getOutPoint();
      result->txHashSet.insert(op.getTxHash());
      result->txOutIdx[op.getTxHash()].insert(op.getTxOutIndex());
   }
   if (result->txHashSet.empty()) {
      cb(false);
   }
   else {
      armory->getTXsByHash(result->txHashSet, cbTXs, true);
   }
}

bool TxChecker::hasInput(const BinaryData &txHash) const
{
   if (!tx_.isInitialized()) {
      return false;
   }
   for (size_t i = 0; i < tx_.getNumTxIn(); ++i) {
      TxIn in = tx_.getTxInCopy((int)i);
      if (!in.isInitialized()) {
         continue;
      }
      OutPoint op = in.getOutPoint();
      if (op.getTxHash() == txHash) {
         return true;
      }
   }
   return false;
}


NetworkType getNetworkType()
{
   switch (Armory::Config::BitcoinSettings::getMode()) {
   case Armory::Config::NETWORK_MODE_MAINNET: return NetworkType::MainNet;
   case Armory::Config::NETWORK_MODE_TESTNET: return NetworkType::TestNet;
   case Armory::Config::NETWORK_MODE_REGTEST: return NetworkType::RegTest;
   default:       return NetworkType::RegTest;
   }
}

uint64_t bs::estimateVSize(const Signer &signer)
{
   size_t baseSize = 10;
   size_t witnessSize = 0;
   for (uint32_t i = 0; i < signer.getTxInCount(); ++i) {
      const auto &addr = bs::Address::fromUTXO(signer.getSpender(i)->getUtxo());
      baseSize += addr.getInputSize();
      witnessSize += addr.getWitnessDataSize();
   }
   for (const auto &recipients : signer.getRecipientMap()) {
      for (const auto &recipient : recipients.second) {
         baseSize += recipient->getSize();
      }
   }
   auto weight = 4 * baseSize + witnessSize;
   uint64_t vsize = (weight + 3) / 4;
   return vsize;
}
