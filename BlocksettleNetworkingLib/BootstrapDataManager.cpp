/*

***********************************************************************************
* Copyright (C) 2020 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "BootstrapDataManager.h"

#include "ApplicationSettings.h"
#include "EncryptionUtils.h"
#include "Signer.h"

#include <spdlog/spdlog.h>

#include <QFile>

#include "bs_communication.pb.h"

using namespace Blocksettle::Communication;


BootstrapDataManager::BootstrapDataManager(const std::shared_ptr<spdlog::logger> &logger
   , const std::shared_ptr<ApplicationSettings> &appSettings)
   : logger_{ logger }
   , appSettings_(appSettings)
{
}

bool BootstrapDataManager::hasLocalFile() const
{
   return QFile(appSettings_->bootstrapFilePath()).exists();
}

bool BootstrapDataManager::loadFromLocalFile()
{
   if (!hasLocalFile()) {
      logger_->error("[BootstrapDataManager::loadFromLocalFile] local file missing");
      return false;
   }

   QFile f(appSettings_->bootstrapFilePath());

   if (!f.open(QIODevice::ReadOnly)) {
      return false;
   }

   const auto buf = f.readAll();
   if (buf.isEmpty()) {
      return false;
   }

   return loadData(buf.toStdString());
}

bool BootstrapDataManager::setReceivedData(const std::string& data)
{
   if (loadData(data)) {
      saveToLocalFile(data);
      return true;
   }

   return false;
}

bool BootstrapDataManager::loadData(const std::string& data)
{
   if (data.empty()) {
      logger_->error("[BootstrapDataManager::loadData] empty data");
      return false;
   }

   ResponsePacket response;
   if (!response.ParseFromString(data)) {
      logger_->error("[BootstrapDataManager::loadData] failed to parse bootstrap data");
      return false;
   }

   if (response.responsetype() != RequestType::BootstrapSignedDataType) {
      logger_->error("[BootstrapDataManager::loadData] undefined response type {}"
                     , static_cast<int>(response.responsetype()));
      return false;
   }

   const auto payload = BinaryData::fromString(response.responsedata());
   const auto signature = BinaryData::fromString(response.datasignature());
   const auto signAddress = bs::Address::fromAddressString(appSettings_->GetBlocksettleSignAddress()).prefixed();

   if (!ArmorySigner::Signer::verifyMessageSignature(payload, signAddress, signature)) {
      logger_->error("[BootstrapDataManager::loadData] signature invalid");
      return false;
   }

   return processBootstrapData(response.responsedata());
}

bool BootstrapDataManager::processBootstrapData(const std::string& rawString)
{
   BootstrapData data;

   if (!data.ParseFromString(rawString)) {
      logger_->error("[BootstrapDataManager::processBootstrapData] data corrupted. Could not parse.");
      return false;
   }

   if (data.is_testnet() != (appSettings_->get<NetworkType>(ApplicationSettings::netType) == NetworkType::TestNet)) {
      logger_->error("[BootstrapDataManager::processBootstrapData] network type mismatch in reply");
      return false;
   }

   if (data.revision() < currentRev_) {
      logger_->error("[BootstrapDataManager::processBootstrapData] proxy has older revision {} than we ({})"
         , data.revision(), currentRev_);
      return false;
   }

   if (data.revision() == currentRev_) {
      logger_->debug("[BootstrapDataManager::processBootstrapData] having the same revision already");
      return true;
   }

   if (data.proxy_keys_size() == 0
     || data.armory_mainnet_keys_size() == 0
     || data.armory_testnet_keys_size() == 0
     || data.chat_keys_size() == 0
     || data.cc_tracker_keys_size() == 0
     || data.validation_address_size() == 0) {
      return false;
   }

   // load keys
   proxyKey_ = data.proxy_keys(0);
   chatKey_ = data.chat_keys(0);
   ccTrackerKey_ = data.cc_tracker_keys(0);

   mainnetArmoryKey_ = data.armory_mainnet_keys(0);
   testnetArmoryKey_ = data.armory_testnet_keys(0);

   validationAddresses_ = std::unordered_set<std::string>(data.validation_address().begin(), data.validation_address().end());

   ccDefinitions_.clear();

   for (const auto &ccSecurity : data.cc_securities()) {
      bs::network::CCSecurityDef ccSecDef = {
         ccSecurity.securityid(), ccSecurity.product(),
         bs::Address::fromAddressString(ccSecurity.genesisaddr()), ccSecurity.satoshisnb()
      };
      ccDefinitions_.emplace_back(ccSecDef);
   }

   currentRev_ = data.revision();

   return true;
}

bool BootstrapDataManager::saveToLocalFile(const std::string &data)
{
   const auto path = appSettings_->bootstrapFilePath();

   QFile f(path);
   if (!f.open(QIODevice::WriteOnly)) {
      logger_->error("[BootstrapDataManager::saveToLocalFile] failed to open file {} for writing", path.toStdString());
      return false;
   }

   const auto writeSize = f.write(data.data(), static_cast<int>(data.size()));
   if (static_cast<int>(data.size()) != writeSize) {
      logger_->error("[BootstrapDataManager::saveToLocalFile] failed to write to {}", path.toStdString());
      return false;
   }

   return true;
}

std::string BootstrapDataManager::getProxyKey() const
{
   return proxyKey_;
}

std::string BootstrapDataManager::getChatKey() const
{
   return chatKey_;
}

std::string BootstrapDataManager::getCCTrackerKey() const
{
   return ccTrackerKey_;
}

std::string BootstrapDataManager::getArmoryTestnetKey() const
{
   return testnetArmoryKey_;
}

std::string BootstrapDataManager::getArmoryMainnetKey() const
{
   return mainnetArmoryKey_;
}

std::unordered_set<std::string> BootstrapDataManager::GetAuthValidationList() const
{
   return validationAddresses_;
}
std::vector<bs::network::CCSecurityDef> BootstrapDataManager::GetCCDefinitions() const
{
   return ccDefinitions_;
}
