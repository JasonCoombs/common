/*

***********************************************************************************
* Copyright (C) 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/

#include "ColoredCoinCache.h"

#include "ColoredCoinLogic.h"
#include "cc_snapshots.pb.h"

namespace {

   std::shared_ptr<BinaryData> getOrInsert(const BinaryData &hash, std::map<BinaryData, std::shared_ptr<BinaryData>> &txHashes)
   {
      auto &elem = txHashes[hash];
      if (!elem) {
         elem = std::make_shared<BinaryData>(hash);
      }
      return elem;
   }

   void serializeUtxoSet(const CcUtxoSet &utxoSet, bs::cc_snapshots::ColoredCoinSnapshot &msg)
   {
      for (const auto &utxo : utxoSet) {
         for (const auto &item : utxo.second) {
            const auto &point = *item.second;
            auto d = msg.add_outpoints();
            d->set_value(point.value());
            d->set_index(point.index());
            d->set_tx_hash(point.getTxHash()->toBinStr());
            d->set_scr_addr(point.getScrAddr()->toBinStr());
         }
      }
   }

   bool deserializeUtxoSet(const bs::cc_snapshots::ColoredCoinSnapshot &msg, CcUtxoSet &utxoSet, ScrAddrCcSet &scrAddrCcSet)
   {
      std::map<BinaryData, std::shared_ptr<BinaryData>> txHashes;
      std::map<BinaryData, std::shared_ptr<BinaryData>> scrAddresses;

      for (const auto &d : msg.outpoints()) {
         auto point = std::make_shared<CcOutpoint>(d.value(), d.index());
         const auto txHash = BinaryData::fromString(d.tx_hash());
         const auto scrAddr = BinaryData::fromString(d.scr_addr());

         point->setTxHash(getOrInsert(txHash, txHashes));
         point->setScrAddr(getOrInsert(scrAddr, scrAddresses));

         auto &utxo = utxoSet[txHash];
         auto utxoItem = utxo.emplace(point->index(), point);
         if (!utxoItem.second) {
            return false;
         }

         auto &scrAddrSet = scrAddrCcSet[scrAddr];
         auto scrAddrItem = scrAddrSet.emplace(point);
         if (!scrAddrItem.second) {
            return false;
         }
      }

      return true;
   }

   void serializeOutPointsSet(const OutPointsSet &outPoints, bs::cc_snapshots::ColoredCoinSnapshot &msg)
   {
      for (const auto &spentOutput : outPoints) {
         auto d = msg.add_spent_outputs();
         d->set_tx_hash(spentOutput.first.toBinStr());
         for (auto index : spentOutput.second) {
            d->add_index(index);
         }
      }
   }

   void deserializeOutPointsSet(const bs::cc_snapshots::ColoredCoinSnapshot &msg, OutPointsSet &outPoints)
   {
      for (const auto &d : msg.spent_outputs()) {
         auto txHash = BinaryData::fromString(d.tx_hash());
         std::set<unsigned> indices;
         for (auto index : d.index()) {
            indices.insert(index);
         }
         outPoints.emplace(std::move(txHash), std::move(indices));
      }
   }

} // namespace

std::string serializeColoredCoinSnapshot(const std::shared_ptr<ColoredCoinSnapshot> &snapshot)
{
   if (!snapshot) {
      return {};
   }

   bs::cc_snapshots::ColoredCoinSnapshot msg;

   serializeUtxoSet(snapshot->utxoSet_, msg);

   serializeOutPointsSet(snapshot->txHistory_, msg);

   for (const auto &revoked : snapshot->revokedAddresses_) {
      auto d = msg.add_revoked_addresses();
      d->set_scr_addr(revoked.first.toBinStr());
      d->set_height(revoked.second);
   }

   return msg.SerializeAsString();
}

std::string serializeColoredCoinZcSnapshot(const std::shared_ptr<ColoredCoinZCSnapshot> &snapshot)
{
   if (!snapshot) {
      return {};
   }

   bs::cc_snapshots::ColoredCoinSnapshot msg;

   serializeUtxoSet(snapshot->utxoSet_, msg);

   serializeOutPointsSet(snapshot->spentOutputs_, msg);

   return msg.SerializeAsString();
}

std::shared_ptr<ColoredCoinSnapshot> deserializeColoredCoinSnapshot(const std::string &data)
{
   bs::cc_snapshots::ColoredCoinSnapshot msg;
   bool result = msg.ParseFromString(data);
   if (!result) {
      return {};
   }

   auto snapshot = std::make_shared<ColoredCoinSnapshot>();

   result = deserializeUtxoSet(msg, snapshot->utxoSet_, snapshot->scrAddrCcSet_);
   if (!result) {
      return {};
   }

   deserializeOutPointsSet(msg, snapshot->txHistory_);

   for (const auto &d : msg.revoked_addresses()) {
      snapshot->revokedAddresses_[BinaryData::fromString(d.scr_addr())] = d.height();
   }

   return snapshot;
}

std::shared_ptr<ColoredCoinZCSnapshot> deserializeColoredCoinZcSnapshot(const std::string &data)
{
   bs::cc_snapshots::ColoredCoinSnapshot msg;
   bool result = msg.ParseFromString(data);
   if (!result) {
      return {};
   }

   auto snapshot = std::make_shared<ColoredCoinZCSnapshot>();

   result = deserializeUtxoSet(msg, snapshot->utxoSet_, snapshot->scrAddrCcSet_);
   if (!result || snapshot->utxoSet_.empty()) {
      return {};
   }

   deserializeOutPointsSet(msg, snapshot->spentOutputs_);

   return snapshot;
}
