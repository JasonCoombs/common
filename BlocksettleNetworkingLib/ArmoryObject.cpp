/*

***********************************************************************************
* Copyright (C) 2016 - , BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "ArmoryObject.h"

#include <QFile>
#include <QProcess>
#include <QPointer>

#include <cassert>
#include <exception>
#include <condition_variable>

#include <spdlog/spdlog.h>

#include "ClientClasses.h"
#include "DbHeader.h"
#include "EncryptionUtils.h"
#include "FastLock.h"
#include "JSON_codec.h"
#include "ManualResetEvent.h"
#include "SocketIncludes.h"

namespace {

   const int kDefaultArmoryDBStartTimeoutMsec = 500;

   const uint32_t kRequiredConfCountForCache = 6;

} // namespace


ArmoryObject::ArmoryObject(const std::shared_ptr<spdlog::logger> &logger
   , const std::string &txCacheFN, bool cbInMainThread)
   : ArmoryConnection(logger)
   , cbInMainThread_(cbInMainThread)
   , txCache_(txCacheFN)
{}

bool ArmoryObject::startLocalArmoryProcess(const ArmorySettings &settings)
{
   if (armoryProcess_ && (armoryProcess_->state() == QProcess::Running)) {
      logger_->info("[{}] Armory process {} is already running with PID {}"
         , __func__, settings.armoryExecutablePath.toStdString()
         , armoryProcess_->processId());
      return true;
   }
   const QString armoryDBPath = settings.armoryExecutablePath;
   if (QFile::exists(armoryDBPath)) {
      armoryProcess_ = std::make_shared<QProcess>();

      QStringList args;
      switch (settings.netType) {
      case NetworkType::TestNet:
         args.append(QString::fromStdString("--testnet"));
         break;
      case NetworkType::RegTest:
         args.append(QString::fromStdString("--regtest"));
         break;
      default: break;
      }

//      args.append(QLatin1String("--db-type=DB_FULL"));
      args.append(QLatin1String("--listen-port=") + QString::number(settings.armoryDBPort));
      args.append(QLatin1String("--satoshi-datadir=\"") + settings.bitcoinBlocksDir + QLatin1String("\""));
      args.append(QLatin1String("--dbdir=\"") + settings.dbDir + QLatin1String("\""));
      args.append(QLatin1String("--public"));

      logger_->debug("[{}] running {} {}", __func__, settings.armoryExecutablePath.toStdString()
         , args.join(QLatin1Char(' ')).toStdString());
      armoryProcess_->start(settings.armoryExecutablePath, args);
      if (armoryProcess_->waitForStarted(kDefaultArmoryDBStartTimeoutMsec)) {
         return true;
      }
      armoryProcess_.reset();
   }
   return false;
}

bool ArmoryObject::needInvokeCb() const
{
   return cbInMainThread_ && (thread() != QThread::currentThread());
}

void ArmoryObject::setupConnection(const ArmorySettings &settings, const BIP151Cb &bip150PromptUserCb)
{
   if (settings.runLocally) {
      if (!startLocalArmoryProcess(settings)) {
         logger_->error("[{}] failed to start Armory from {}", __func__
                        , settings.armoryExecutablePath.toStdString());
         setState(ArmoryState::Offline);
         return;
      }
   }

   // Add BIP 150 server keys
   BinaryData serverBIP15xKey;
   if (!settings.armoryDBKey.isEmpty()) {
      try {
         serverBIP15xKey = READHEX(settings.armoryDBKey.toStdString());
      } catch (const std::exception &e) {
         logger_->error("invalid armory key detected: {}: {}", settings.armoryDBKey.toStdString(), e.what());
      }
   }

   ArmoryConnection::setupConnection(settings.netType, settings.armoryDBIp.toStdString()
      , std::to_string(settings.armoryDBPort), settings.dataDir.toStdString(), serverBIP15xKey
      , settings.password, bip150PromptUserCb);
}

bool ArmoryObject::getWalletsHistory(const std::vector<std::string> &walletIDs, const WalletsHistoryCb &cb)
{
   const auto &cbWrap = [this, cb](std::vector<ClientClasses::LedgerEntry> le) {
      if (!cb) {
         return;
      }
      if (needInvokeCb()) {
         QMetaObject::invokeMethod(this, [cb, le] { cb(std::move(le)); });
      } else {
         cb(std::move(le));
      }
   };
   return ArmoryConnection::getWalletsHistory(walletIDs, cbWrap);
}

bool ArmoryObject::getWalletsLedgerDelegate(const LedgerDelegateCb &cb)
{
   const auto &cbWrap = [this, cb](const std::shared_ptr<AsyncClient::LedgerDelegate> &ld) {
      if (!cb) {
         return;
      }
      if (needInvokeCb()) {
         QMetaObject::invokeMethod(this, [cb, ld] {
            cb(ld);
         });
      } else {
         cb(ld);
      }
   };
   return ArmoryConnection::getWalletsLedgerDelegate(cbWrap);
}

bool ArmoryObject::getTxByHash(const BinaryData &hash, const TxCb &cb, bool allowCachedResult)
{
   if (allowCachedResult) {
      const auto tx = getFromCache(hash);
      if (tx) {
         if (needInvokeCb()) {
            QMetaObject::invokeMethod(this, [cb, tx] {
               cb(*tx);
            });
         } else {
            cb(*tx);
         }
         return true;
      }
   }
   const auto &cbWrap = [this, cb, hash](Tx tx) {
      putToCacheIfNeeded(hash, std::make_shared<Tx>(tx));
      if (!cb) {
         return;
      }
      if (needInvokeCb()) {
         QMetaObject::invokeMethod(this, [cb, tx] { cb(tx); });
      }
      else {
         cb(tx);
      }
   };
   return ArmoryConnection::getTxByHash(hash, cbWrap, allowCachedResult);
}

bool ArmoryObject::getTXsByHash(const std::set<BinaryData> &hashes, const TXsCb &cb, bool allowCachedResult)
{
   auto result = std::make_shared<AsyncClient::TxBatchResult>();

   std::set<BinaryData> missedHashes;
   if (allowCachedResult) {
      for (const auto &hash : hashes) {
         const auto tx = getFromCache(hash);
         if (tx) {
            (*result)[hash] = tx;
         }
         else {
            missedHashes.insert(hash);
         }
      }
   } else {
      missedHashes = hashes;
   }

   if (missedHashes.empty()) {
      if (needInvokeCb()) {
         QMetaObject::invokeMethod(this, [cb, result] {
            cb(*result, nullptr);
         });
      } else {
         cb(*result, nullptr);
      }
      return true;
   }
   const auto &cbWrap = [this, cb, result]
      (const AsyncClient::TxBatchResult &txs, std::exception_ptr exPtr)
   {
      if (exPtr != nullptr) {
         cb({}, exPtr);
         return;
      }
      for (const auto &tx : txs) {
         putToCacheIfNeeded(tx.first, tx.second);
         (*result)[tx.first] = tx.second;
      }
      if (needInvokeCb()) {
         QMetaObject::invokeMethod(this, [cb, result] { cb(*result, nullptr); });
      }
      else {
         cb(*result, nullptr);
      }
   };
   return ArmoryConnection::getTXsByHash(missedHashes, cbWrap, allowCachedResult);
}

bool ArmoryObject::getRawHeaderForTxHash(const BinaryData& inHash, const BinaryDataCb &callback)
{
   const auto &cbWrap = [this, callback](BinaryData header) {
      if (!callback) {
         return;
      }
      if (needInvokeCb()) {
         QMetaObject::invokeMethod(this, [callback, header] { callback(std::move(header)); });
      } else {
         callback(header);
      }
   };
   return ArmoryConnection::getRawHeaderForTxHash(inHash, cbWrap);
}

bool ArmoryObject::getHeaderByHeight(const unsigned int inHeight, const BinaryDataCb &callback)
{
   const auto &cbWrap = [this, callback](BinaryData header) {
      if (!callback) {
         return;
      }
      if (needInvokeCb()) {
         QMetaObject::invokeMethod(this, [callback, header] { callback(std::move(header)); });
      } else {
         callback(header);
      }
   };
   return ArmoryConnection::getHeaderByHeight(inHeight, cbWrap);
}

// Frontend for Armory's estimateFee() call. Used to get the "economical" fee
// that Bitcoin Core estimates for successful insertion into a block within a
// given number (2-1008) of blocks.
bool ArmoryObject::estimateFee(unsigned int nbBlocks, const FloatCb &cb)
{
   const auto &cbWrap = [this, cb](float fee) {
      if (!cb) {
         return;
      }
      if (needInvokeCb()) {
         QMetaObject::invokeMethod(this, [cb, fee] { cb(fee); });
      } else {
         cb(fee);
      }
   };
   return ArmoryConnection::estimateFee(nbBlocks, cbWrap);
}

// Frontend for Armory's getFeeSchedule() call. Used to get the range of fees
// that Armory caches. The fees/byte are estimates for what's required to get
// successful insertion of a TX into a block within X number of blocks.
bool ArmoryObject::getFeeSchedule(const FloatMapCb &cb)
{
   const auto &cbWrap = [this, cb](std::map<unsigned int, float> fees) {
      if (!cb) {
         return;
      }
      if (needInvokeCb()) {
         QMetaObject::invokeMethod(this, [cb, fees] { cb(fees); });
      }
      else {
         cb(fees);
      }
   };
   return ArmoryConnection::getFeeSchedule(cbWrap);
}

std::shared_ptr<const Tx> ArmoryObject::getFromCache(const BinaryData &hash)
{
   return txCache_.get(hash);
}

void ArmoryObject::putToCacheIfNeeded(const BinaryData &hash, const std::shared_ptr<const Tx> &tx)
{
   if (!tx || !tx->isInitialized() || tx->getTxHeight() == UINT32_MAX) {
      return;
   }
   const auto topBlock = topBlock_.load();
   if (topBlock == 0 || topBlock == UINT32_MAX) {
      return;
   }

   if (tx->getTxHeight() > topBlock) {
      // should not happen
      SPDLOG_LOGGER_ERROR(logger_, "invalid tx height: {}, topBlock: {}", tx->getTxHeight(), topBlock);
      return;
   }
   if (topBlock - tx->getTxHeight() < kRequiredConfCountForCache) {
      return;
   }

   try {
      txCache_.put(hash, tx);
   } catch (const std::exception &e) {
      SPDLOG_LOGGER_ERROR(logger_, "caching tx failed: {}", e.what());
   }
}
