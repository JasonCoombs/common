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
#include "CelerClient.h"
#include "ConnectionManager.h"
#include "EncryptionUtils.h"

#include <spdlog/spdlog.h>

#include <QFile>

#include "bs_communication.pb.h"
#include "bs_storage.pb.h"

using namespace Blocksettle::Communication;


BootstrapDataManager::BootstrapDataManager(const std::shared_ptr<spdlog::logger> &logger
   , const std::shared_ptr<ApplicationSettings> &appSettings)
   : appSettings_(appSettings)
   , signAddress_(bs::Address::fromAddressString(appSettings_->GetBlocksettleSignAddress()))
   , bootstapFilePath_(appSettings->bootstrapFilePath())
{
}

bool BootstrapDataManager::hasLocalFile() const
{
   return QFile(bootstapFilePath_).exists();
}

void BootstrapDataManager::setReceivedData(const BinaryData &data)
{
   if (data.empty()) {
      return;
   }

   ResponsePacket response;
   if (!response.ParseFromArray(data.getPtr(), static_cast<int>(data.getSize()))) {
      SPDLOG_LOGGER_ERROR(logger_, "failed to parse response from public bridge");
      return;
   }

   switch (response.responsetype()) {
      case RequestType::BootstrapSignedDataType:
         processResponse(response.responsedata(), response.datasignature());
         break;
      default:
         break;
   }
}

CcGenFileError BootstrapDataManager::loadSavedData()
{
   auto loadError = loadFromFile(bootstapFilePath_.toStdString(), appSettings_->get<NetworkType>(ApplicationSettings::netType));
   if (loadError != CcGenFileError::NoError) {
      QFile::remove(bootstapFilePath_);
   }
   return loadError;
}

void BootstrapDataManager::processResponse(const std::string &response, const std::string &sig)
{
   bool sigVerified = verifySignature(BinaryData::fromString(response), BinaryData::fromString(sig), signAddress_);
   if (!sigVerified) {
      SPDLOG_LOGGER_ERROR(logger_, "signature verification failed! Rejecting CC genesis addresses reply.");
      return;
   }

   BootstrapData data;

   if (!data.ParseFromString(response)) {
      SPDLOG_LOGGER_ERROR(logger_, "data corrupted. Could not parse.");
      return;
   }

   if (data.is_testnet() != (appSettings_->get<NetworkType>(ApplicationSettings::netType) == NetworkType::TestNet)) {
      SPDLOG_LOGGER_ERROR(logger_, "network type mismatch in reply");
      return;
   }

   if (data.revision() == currentRev_) {
      SPDLOG_LOGGER_DEBUG(logger_, "having the same revision already");
      return;
   }

   if (data.revision() < currentRev_) {
      SPDLOG_LOGGER_ERROR(logger_, "proxy has older revision {} than we ({})"
         , data.revision(), currentRev_);
      return;
   }

   fillFrom(data);

   saveToFile(bootstapFilePath_.toStdString(), response, sig);
}

bool BootstrapDataManager::saveToFile(const std::string &path, const std::string &response, const std::string &sig)
{
   Blocksettle::Storage::CCDefinitions msg;
   msg.set_response(response);
   msg.set_signature(sig);
   auto data = msg.SerializeAsString();

   QFile f(QString::fromStdString(path));
   if (!f.open(QIODevice::WriteOnly)) {
      SPDLOG_LOGGER_ERROR(logger_, "failed to open file {} for writing", path);
      return false;
   }

   auto writeSize = f.write(response.data(), static_cast<int>(response.size()));
   if (static_cast<int>(response.size()) != writeSize) {
      SPDLOG_LOGGER_ERROR(logger_, "failed to write to {}", path);
      return false;
   }

   return true;
}

bool BootstrapDataManager::isTestNet() const
{
   return appSettings_->get<NetworkType>(ApplicationSettings::netType) != NetworkType::MainNet;
}


bool BootstrapDataManager::verifySignature(const BinaryData &data, const BinaryData &sign, const bs::Address &signAddress)
{
   return ArmorySigner::Signer::verifyMessageSignature(data, signAddress.prefixed(), sign);
}

void BootstrapDataManager::fillFrom(const BootstrapData &data)
{
   // TODO: Use somehow
}

CcGenFileError BootstrapDataManager::loadFromFile(const std::string &path, NetworkType netType)
{
   QFile f(QString::fromStdString(path));
   if (!f.exists()) {
      SPDLOG_LOGGER_DEBUG(logger_, "no bootstrap file to load at {}", path);
      return CcGenFileError::ReadError;
   }
   if (!f.open(QIODevice::ReadOnly)) {
      SPDLOG_LOGGER_ERROR(logger_, "failed to open file {} for reading", path);
      return CcGenFileError::ReadError;
   }

   const auto buf = f.readAll();
   if (buf.isEmpty()) {
      SPDLOG_LOGGER_ERROR(logger_, "failed to read from {}", path);
      return CcGenFileError::ReadError;
   }

   Blocksettle::Storage::CCDefinitions msg;
   bool result = msg.ParseFromArray(buf.data(), buf.size());
   if (!result) {
      SPDLOG_LOGGER_ERROR(logger_, "failed to parse storage file");
      return CcGenFileError::InvalidFormat;
   }

   result = verifySignature(BinaryData::fromString(msg.response()), BinaryData::fromString(msg.signature()), signAddress_);
   if (!result) {
      SPDLOG_LOGGER_ERROR(logger_, "signature verification failed for {}", path);
      return CcGenFileError::InvalidSign;
   }

   BootstrapData data;
   if (!data.ParseFromString(msg.response())) {
      SPDLOG_LOGGER_ERROR(logger_, "failed to parse {}", path);
      return CcGenFileError::InvalidFormat;
   }

   if (data.is_testnet() != (netType == NetworkType::TestNet)) {
      SPDLOG_LOGGER_ERROR(logger_, "wrong network type in {}", path);
      return CcGenFileError::InvalidFormat;
   }

   fillFrom(data);

   return CcGenFileError::NoError;
}
