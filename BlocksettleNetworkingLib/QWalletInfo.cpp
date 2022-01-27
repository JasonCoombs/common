/*

***********************************************************************************
* Copyright (C) 2019 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "QWalletInfo.h"
#include <QFile>
#include "CoreHDWallet.h"
#include "WalletEncryption.h"
#include "WalletBackupFile.h"
#include "Wallets/SyncHDWallet.h"
#include "Wallets/SyncWalletsManager.h"

using namespace bs::hd;
using namespace bs::wallet;
using namespace Blocksettle::Communication;

WalletInfo::WalletInfo(const QString &rootId, const std::vector<EncryptionType> &encTypes
                       , const std::vector<BinaryData> &encKeys, const KeyRank &keyRank)
{
   rootId_ = rootId;
   keyRank_ = keyRank;
   setEncKeys(encKeys);
   setEncTypes(encTypes);
}

WalletInfo::WalletInfo(const headless::GetHDWalletInfoResponse &response)
{
   rootId_ = QString::fromStdString(response.rootwalletid());
   for (int i = 0; i < response.enctypes_size(); ++i) {
      encTypes_.push_back(static_cast<bs::wallet::EncryptionType>(response.enctypes(i)));
   }
   for (int i = 0; i < response.enckeys_size(); ++i) {
      encKeys_.push_back(QString::fromStdString(response.enckeys(i)));
   }
   keyRank_ = { response.rankm(), response.rankn() };
}

WalletInfo::WalletInfo(std::shared_ptr<bs::core::hd::Wallet> hdWallet, QObject *parent)
{
   initFromRootWallet(hdWallet);
   initEncKeys(hdWallet);
}

WalletInfo::WalletInfo(const std::shared_ptr<bs::sync::WalletsManager> &walletsMgr
   , const std::shared_ptr<bs::sync::hd::Wallet> &hdWallet, QObject *parent)
   : walletsMgr_(walletsMgr)
{
   initFromRootWallet(hdWallet);
   initEncKeys(hdWallet);

   if (walletsMgr_) {
      connect(walletsMgr_.get(), &bs::sync::WalletsManager::walletMetaChanged, this, [this, hdWallet]
      (const std::string &walletId) {
         if (walletId == hdWallet->walletId()) {
            initFromRootWallet(hdWallet);
            initEncKeys(hdWallet);
         }
      });
   }
}

WalletInfo::WalletInfo(const bs::sync::WalletInfo &hdWallet, QObject *parent)
{
   if ((hdWallet.format != bs::sync::WalletFormat::HD) || (hdWallet.ids.size() != 1)) {
      throw std::runtime_error("invalid wallet info supplied");
   }
   walletId_ = QString::fromStdString(hdWallet.ids.at(0));
   rootId_ = walletId_;
   name_ = QString::fromStdString(hdWallet.name);
   desc_ = QString::fromStdString(hdWallet.description);

   for (const auto& encKey : hdWallet.encryptionKeys) {
      encKeys_ << QString::fromStdString(encKey.toBinStr());
   }
   for (const auto& encType : hdWallet.encryptionTypes) {
      encTypes_ << encType;
   }
   keyRank_ = hdWallet.encryptionRank;
   emit walletChanged();
}

WalletInfo::WalletInfo(const std::shared_ptr<bs::sync::WalletsManager> &walletsMgr
   , const std::shared_ptr<bs::sync::Wallet> &wallet, QObject *parent)
   : walletsMgr_(walletsMgr)
{
   const auto rootHdWallet = walletsMgr_->getHDRootForLeaf(wallet->walletId());
   initFromWallet(wallet.get(), rootHdWallet->walletId());
   initEncKeys(rootHdWallet);

   if (walletsMgr_) {
      connect(walletsMgr_.get(), &bs::sync::WalletsManager::walletMetaChanged, this, [this, rootHdWallet]
      (const std::string &walletId) {
         if (walletId == rootHdWallet->walletId()) {
            initFromRootWallet(rootHdWallet);
            initEncKeys(rootHdWallet);
         }
      });
   }
}

WalletInfo::WalletInfo(const WalletInfo &other)
   : walletId_(other.walletId_), rootId_(other.rootId_)
   , name_(other.name_), desc_(other.desc_)
   , encKeys_(other.encKeys_), encTypes_(other.encTypes_), keyRank_(other.keyRank_)
{}

WalletInfo &bs::hd::WalletInfo::WalletInfo::operator =(const WalletInfo &other)
{
   walletId_ = other.walletId_;
   rootId_ = other.rootId_;
   name_ = other.name_;
   desc_ = other.desc_;
   encKeys_ = other.encKeys_;
   encTypes_ = other.encTypes_;
   keyRank_ = other.keyRank_;

   return *this;
}

WalletInfo WalletInfo::fromDigitalBackup(const QString &filename)
{
   bs::hd::WalletInfo walletInfo;

   QFile file(filename);
   if (!file.exists()) return walletInfo;

   if (file.open(QIODevice::ReadOnly)) {
      QByteArray data = file.readAll();
      const auto wdb = WalletBackupFile::Deserialize(std::string(data.data(), data.size()));
      walletInfo.setName(QString::fromStdString(wdb.name));
      walletInfo.setDesc(QString::fromStdString(wdb.name));
   }
   return walletInfo;
}

void WalletInfo::initFromWallet(const bs::sync::Wallet *wallet, const std::string &rootId)
{
   if (!wallet)
      return;

   walletId_ = QString::fromStdString(wallet->walletId());
   rootId_ = QString::fromStdString(rootId);
   name_ = QString::fromStdString(wallet->name());
   emit walletChanged();
}

void WalletInfo::initFromRootWallet(const std::shared_ptr<bs::core::hd::Wallet> &rootWallet)
{
   walletId_ = QString::fromStdString(rootWallet->walletId());
   name_ = QString::fromStdString(rootWallet->name());
   rootId_ = QString::fromStdString(rootWallet->walletId());
   keyRank_ = rootWallet->encryptionRank();
   emit walletChanged();
}

void WalletInfo::initEncKeys(const std::shared_ptr<bs::core::hd::Wallet> &rootWallet)
{
   encKeys_.clear();
   setEncKeys(rootWallet->encryptionKeys());
   setEncTypes(rootWallet->encryptionTypes());
}

void WalletInfo::initFromRootWallet(const std::shared_ptr<bs::sync::hd::Wallet> &rootWallet)
{
   walletId_ = QString::fromStdString(rootWallet->walletId());
   name_ = QString::fromStdString(rootWallet->name());
   rootId_ = QString::fromStdString(rootWallet->walletId());
   desc_ = QString::fromStdString(rootWallet->description());
   keyRank_ = rootWallet->encryptionRank();
   emit walletChanged();
}

void WalletInfo::initEncKeys(const std::shared_ptr<bs::sync::hd::Wallet> &rootWallet)
{
   encKeys_.clear();
   setEncKeys(rootWallet->encryptionKeys());
   setEncTypes(rootWallet->encryptionTypes());
}

void WalletInfo::setDesc(const QString &desc)
{
   if (desc_ == desc)
      return;

   desc_ = desc;
   emit walletChanged();
}

void WalletInfo::setWalletId(const QString &walletId)
{
   if (walletId_ == walletId)
      return;

   walletId_ = walletId;
   emit walletChanged();
}

void WalletInfo::setRootId(const QString &rootId)
{
   if (rootId_ == rootId)
      return;

   rootId_ = rootId;
   emit walletChanged();
}

EncryptionType WalletInfo::encType()
{
   if (encTypes_.isEmpty()) {
      return  bs::wallet::EncryptionType::Unencrypted;
   }
   else if (isHardwareWallet()) {
      if (encKeys_.isEmpty()) {
         return bs::wallet::EncryptionType::Unencrypted;
      }

      HardwareEncKey hwKey(BinaryData::fromString(
         encKeys_.at(0).toStdString()));

      if (hwKey.deviceType() == HardwareEncKey::WalletType::Offline) {
         return bs::wallet::EncryptionType::Unencrypted;
      }
   }

   return encTypes_.at(0);
}

void WalletInfo::setEncType(int encType)
{
   encTypes_ = { static_cast<EncryptionType>(encType) };
   emit walletChanged();
}

QString WalletInfo::email() const
{
   if (encKeys_.isEmpty())
      return QString();

   return {};  //FIXME: QString::fromStdString(AutheIDClient::getDeviceInfo(encKeys_.at(0).toStdString()).userId);
}

bool WalletInfo::isEidAuthOnly() const
{
   if (encTypes_.isEmpty())
      return false;

   for (auto encType : encTypes()) {
      if (encType != EncryptionType::Auth) {
         return false;
      }
   }
   return true;
}

bool WalletInfo::isPasswordOnly() const
{
   if (encTypes_.isEmpty())
      return false;

   for (auto encType : encTypes()) {
      if (encType != EncryptionType::Password) {
         return false;
      }
   }
   return true;
}

void WalletInfo::setEncKeys(const std::vector<BinaryData> &encKeys)
{
   encKeys_.clear();
   for (const SecureBinaryData &encKey : encKeys) {
      encKeys_.push_back(QString::fromStdString(encKey.toBinStr()));
   }
   emit walletChanged();
}

void WalletInfo::setEncTypes(const std::vector<EncryptionType> &encTypes)
{
   encTypes_.clear();
   for (const EncryptionType &encType : encTypes) {
      encTypes_.push_back(encType);
   }
   emit walletChanged();
}

void WalletInfo::setPasswordData(const std::vector<PasswordData> &passwordData)
{
   encKeys_.clear();
   encTypes_.clear();

   for (const PasswordData &pw : passwordData) {
      encKeys_.push_back(QString::fromStdString(pw.metaData.encKey.toBinStr()));
      switch (pw.metaData.encType)
      {
      case EncryptionType::Auth:
      case EncryptionType::Password:
      case EncryptionType::Hardware:
         encTypes_.append(pw.metaData.encType);
         break;
      default:
         break;
      }
   }

   emit walletChanged();
}

void WalletInfo::setEncKeys(const QList<QString> &encKeys)
{
   encKeys_ = encKeys;
   emit walletChanged();
}

void WalletInfo::setEncTypes(const QList<EncryptionType> &encTypes)
{
   encTypes_ = encTypes;
   emit walletChanged();
}

void WalletInfo::setName(const QString &name)
{
   if (name_ == name)
      return;

   name_ = name;
   emit walletChanged();
}

bool bs::hd::WalletInfo::isWo() const
{
   return encTypes_.isEmpty()
      || encTypes_[0] == bs::wallet::EncryptionType::Unencrypted;
}

bool bs::hd::WalletInfo::isHardwareWallet() const
{
   return static_cast<bool>(std::count(encTypes_.begin(),
      encTypes_.end(), bs::wallet::EncryptionType::Hardware));
}

#include "moc_SignerUiDefs.cpp"
