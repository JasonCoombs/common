/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "WalletEncryption.h"

//no

/*
BinaryData mergeKeys(const BinaryData &a, const BinaryData &b)
{
   BinaryData result;
   if (a.getSize() > b.getSize()) {
      result = a;
      for (size_t i = 0; i < b.getSize(); ++i) {
         *(result.getPtr() + i) ^= *(b.getPtr() + i);
      }
   }
   else {
      result = b;
      for (size_t i = 0; i < a.getSize(); ++i) {
         *(result.getPtr() + i) ^= *(a.getPtr() + i);
      }
   }
   return result;
}
*/
#include "moc_WalletEncryption.cpp"

bs::wallet::HardwareEncKey::HardwareEncKey(WalletType walletType, const std::string& hwDeviceId)
   : walletType_(walletType)
   , hwDeviceId_(hwDeviceId)
{
}

bs::wallet::HardwareEncKey::HardwareEncKey(BinaryData binaryData)
{
   if (sizeof(uint32_t) >= binaryData.getSize())
      std::logic_error("Incorrect binary data size");

   BinaryReader reader(std::move(binaryData));
   walletType_ = static_cast<WalletType>(reader.get_uint32_t());
   hwDeviceId_ = BinaryDataRef(reader.getCurrPtr(),
      reader.getSizeRemaining()).toBinStr();
}

BinaryData bs::wallet::HardwareEncKey::toBinaryData() const
{
   BinaryWriter packer;
   packer.put_uint32_t(static_cast<uint32_t>(walletType_));
   packer.put_String(hwDeviceId_);
   return packer.getData();
}

std::string bs::wallet::HardwareEncKey::deviceId()
{
   return hwDeviceId_;
}

bs::wallet::HardwareEncKey::WalletType bs::wallet::HardwareEncKey::deviceType()
{
   return walletType_;
}
