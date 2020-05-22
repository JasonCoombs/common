/*

***********************************************************************************
* Copyright (C) 2016 - , BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "ProtobufHeadlessUtils.h"

#include <spdlog/spdlog.h>

#include "CheckRecipSigner.h"
#include "CoreHDLeaf.h"

namespace {

   const int kValidPathLength = 2;

}

headless::SignTxRequest bs::signer::coreTxRequestToPb(const bs::core::wallet::TXSignRequest &txSignReq
   , bool keepDuplicatedRecipients)
{
   headless::SignTxRequest request;
   for (const auto &walletId : txSignReq.walletIds) {
      request.add_walletid(walletId);
   }
   request.set_keepduplicatedrecipients(keepDuplicatedRecipients);

   if (txSignReq.populateUTXOs) {
      request.set_populate_utxos(true);
   }

   for (const auto &utxo : txSignReq.inputs) {
      request.add_inputs(utxo.serialize().toBinStr());
   }
   for (const auto &inputIndex : txSignReq.inputIndices) {
      request.add_input_indices(inputIndex);
   }

   for (const auto &recip : txSignReq.recipients) {
      request.add_recipients(recip->getSerializedScript().toBinStr());
   }

   for (const auto &sortType : txSignReq.outSortOrder) {
      request.add_out_sort_order(static_cast<uint32_t>(sortType));
   }

   if (txSignReq.fee) {
      request.set_fee(txSignReq.fee);
   }

   if (txSignReq.RBF) {
      request.set_rbf(true);
   }

   if (!txSignReq.prevStates.empty()) {
      request.set_unsigned_state(txSignReq.serializeState().SerializeAsString());
   }

   if (txSignReq.change.value) {
      auto change = request.mutable_change();
      change->set_address(txSignReq.change.address.display());
      change->set_index(txSignReq.change.index);
      change->set_value(txSignReq.change.value);
   }

   for (auto& supportingTx : txSignReq.supportingTXs) {
      auto supportingTxMsg = request.add_supportingtxs();
      supportingTxMsg->set_hash(
         supportingTx.first.getPtr(), supportingTx.first.getSize());
      supportingTxMsg->set_rawtx(
         supportingTx.second.getPtr(), supportingTx.second.getSize());
   }

   request.set_allow_broadcasts(txSignReq.allowBroadcasts);
   request.set_expired_timestamp_ms(static_cast<uint64_t>(
      txSignReq.expiredTimestamp.time_since_epoch() / std::chrono::milliseconds(1)));

   return  request;
}

bs::core::wallet::TXSignRequest pbTxRequestToCoreImpl(const headless::SignTxRequest &request)
{
   bs::core::wallet::TXSignRequest txSignReq;

   for (int i = 0; i < request.walletid_size(); ++i) {
      txSignReq.walletIds.push_back(request.walletid(i));
   }
   for (int i = 0; i < request.inputs_size(); i++) {
      UTXO utxo;
      utxo.unserialize(BinaryData::fromString(request.inputs(i)));
      if (utxo.isInitialized()) {
         txSignReq.inputs.push_back(utxo);
      }
   }
   for (const auto &inputIndex : request.input_indices()) {
      if (!inputIndex.empty()) {
         if (bs::hd::Path::fromString(inputIndex).length() != kValidPathLength) {
            throw std::runtime_error("unexpected path length for UTXO input address");
         }
      }
      txSignReq.inputIndices.push_back(inputIndex);
   }

   uint64_t outputVal = 0;
   for (int i = 0; i < request.recipients_size(); i++) {
      auto serialized = BinaryData::fromString(request.recipients(i));
      const auto recip = ScriptRecipient::deserialize(serialized);
      txSignReq.recipients.push_back(recip);
      outputVal += recip->getValue();
   }

   if (request.out_sort_order_size() == 3) {
      txSignReq.outSortOrder = { static_cast<bs::core::wallet::OutputOrderType>(request.out_sort_order(0))
         , static_cast<bs::core::wallet::OutputOrderType>(request.out_sort_order(1))
         , static_cast<bs::core::wallet::OutputOrderType>(request.out_sort_order(2)) };
   }

   if (request.has_change() && request.change().value()) {
      if (!request.change().index().empty()) {
         if (bs::hd::Path::fromString(request.change().index()).length() != kValidPathLength) {
            throw std::runtime_error("unexpected path length "
               + std::to_string(request.change().index().length()) + " for change address");
         }
         txSignReq.change.index = request.change().index();
      }
      txSignReq.change.address = bs::Address::fromAddressString(request.change().address());
      txSignReq.change.value = request.change().value();
   }

   int64_t value = outputVal;

   txSignReq.fee = request.fee();
   txSignReq.RBF = request.rbf();

   if (!request.unsigned_state().empty()) {
      Codec_SignerState::SignerState state;
      state.ParseFromString(request.unsigned_state());
      txSignReq.prevStates.push_back(state);
      if (!value) {
         bs::CheckRecipSigner signer(state);
         value = signer.outputsTotalValue();
         if (txSignReq.change.value) {
            value -= txSignReq.change.value;
         }
      }
   }

   txSignReq.populateUTXOs = request.populate_utxos();

   for (unsigned i=0; i<request.supportingtxs_size(); i++)
   {
      const auto& supportingtx = request.supportingtxs(i);
      auto txHash = BinaryData::fromString(supportingtx.hash());
      auto rawTx = BinaryData::fromString(supportingtx.rawtx());

      txSignReq.supportingTXs.emplace(txHash, rawTx);
   }

   txSignReq.allowBroadcasts = request.allow_broadcasts();
   txSignReq.expiredTimestamp = std::chrono::system_clock::time_point{}
      + std::chrono::milliseconds(request.expired_timestamp_ms());

   return txSignReq;
}

bs::core::wallet::TXSignRequest bs::signer::pbTxRequestToCore(const headless::SignTxRequest &request
   , const std::shared_ptr<spdlog::logger> &logger)
{
   try {
      return pbTxRequestToCoreImpl(request);
   } catch (const std::exception &e) {
      if (logger) {
         SPDLOG_LOGGER_ERROR(logger, "deserialization sign request failed: {}", e.what());
      }
      return {};
   }
}
