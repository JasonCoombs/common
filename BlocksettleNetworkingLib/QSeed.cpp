/*

***********************************************************************************
* Copyright (C) 2019 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "QSeed.h"
#include <QFile>
#include <QVariant>
#include <QStandardPaths>
#include <QTextStream>
#include "ArmoryBackups.h"
#include "WalletBackupFile.h"

using namespace bs::wallet;

bs::wallet::QSeed::QNetworkType QSeed::toQNetworkType(NetworkType netType)
{
   return static_cast<QNetworkType>(netType);
}

NetworkType QSeed::fromQNetworkType(QNetworkType netType)
{
   return static_cast<NetworkType>(netType);
}

QSeed QSeed::fromPaperKey(const QString &key, QNetworkType netType)
{
   QSeed seed;
   try {
      const auto seedLines = key.split(QLatin1String("\n"), QString::SkipEmptyParts);
      if (seedLines.count() == 2) {
         const auto& decoded = ArmoryBackups::BackupEasy16::decode({ seedLines[0].toStdString()
            , seedLines[1].toStdString() });
/*         if (static_cast<ArmoryBackups::BackupType>(decoded.checksumIndexes_.at(0))
            != ArmoryBackups::BackupType::BIP32_Seed_Structured) {
            throw std::invalid_argument("invalid backup type " + std::to_string(decoded.checksumIndexes_.at(0)));
         }*/
         seed = QSeed(decoded.data_, fromQNetworkType(netType));
      }
      else {
         throw std::runtime_error("invalid seed string line count");
      }
   }
   catch (const std::exception &e) {
      seed.lastError_ = tr("Failed to parse wallet key: %1").arg(QLatin1String(e.what()));
      throw std::runtime_error("unexpected seed string");
   }
   return seed;
}

QSeed QSeed::fromDigitalBackup(const QString &filename, QNetworkType netType)
{
   QSeed seed;

   QFile file(filename);
   if (!file.exists()) {
      seed.lastError_ = tr("Digital Backup file %1 doesn't exist").arg(filename);
      return seed;
   }
   if (file.open(QIODevice::ReadOnly)) {
      QByteArray data = file.readAll();
      const auto wdb = WalletBackupFile::Deserialize(std::string(data.data(), data.size()));
      if (wdb.id.empty()) {
         seed.lastError_ = tr("Digital Backup file %1 corrupted").arg(filename);
      }
      else {
         try {
            const auto& decoded = ArmoryBackups::BackupEasy16::decode({ wdb.seed.part1
               , wdb.seed.part2 });
/*            if (static_cast<ArmoryBackups::BackupType>(decoded.checksumIndexes_.at(0))
               != ArmoryBackups::BackupType::BIP32_Seed_Structured) {
               throw std::invalid_argument("invalid backup type "
                  + std::to_string(decoded.checksumIndexes_.at(0)));
            }*/   // this check fails for some reason
            seed = QSeed(decoded.data_, fromQNetworkType(netType));
         }
         catch (const std::exception& e) {
            seed.lastError_ = tr("Failed to decode wallet seed: %1").arg(QLatin1String(e.what()));
         }
      }
   }
   else {
      seed.lastError_ = tr("Failed to read Digital Backup file %1").arg(filename);
   }
   return seed;
}

QSeed bs::wallet::QSeed::fromMnemonicWordList(const QString &sentence,
   QNetworkType netType, const std::vector<std::vector<std::string>>& dictionaries)
{
   return bs::core::wallet::Seed::fromBip39(sentence.toStdString(),
      fromQNetworkType(netType), dictionaries);
}
