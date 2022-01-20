/*

***********************************************************************************
* Copyright (C) 2020 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "BIP15xMessage.h"
#include "BIP15x_Handshake.h"

using namespace bs::network::bip15x;

void MessageBuilder::construct(const uint8_t *data, uint32_t dataSize, ArmoryAEAD::BIP151_PayloadType type)
{
   construct(data, dataSize, (uint8_t)type);
}

void MessageBuilder::construct(const uint8_t *data, uint32_t dataSize, uint8_t type)
{
   BinaryWriter writer;
   // Store packet length, will be used in chacha20poly1305_get_length later
   writer.put_uint32_t(0);
   writer.put_uint8_t(static_cast<uint8_t>(type));
   writer.put_BinaryData(data, dataSize);
   packet_ = writer.getData();
   uint32_t packetSize = uint32_t(packet_.getSize() - sizeof(uint32_t));
   std::memcpy(packet_.getPtr(), &packetSize, sizeof(packetSize));
}

MessageBuilder::MessageBuilder(const uint8_t *data, uint32_t dataSize, uint8_t type)
{
   construct(data, dataSize, type);
}

MessageBuilder::MessageBuilder(const std::vector<uint8_t> &data, uint8_t type)
{
   construct(data.data(), data.size(), type);
}

MessageBuilder::MessageBuilder(const BinaryDataRef &data, ArmoryAEAD::BIP151_PayloadType type)
{
   construct(data.getPtr(), data.getSize(), type);
}

MessageBuilder::MessageBuilder(const BinaryDataRef &data, uint8_t type)
{
   construct(data.getPtr(), data.getSize(), type);
}

MessageBuilder::MessageBuilder(const std::string &data, uint8_t type)
{
   construct(reinterpret_cast<const uint8_t*>(data.data()), data.size(), type);
}

MessageBuilder::MessageBuilder(uint8_t type)
{
   construct(nullptr, 0, type);
}

MessageBuilder &MessageBuilder::encryptIfNeeded(BIP151Connection *conn)
{
   if (!conn) {
      return *this;
   }

   size_t plainTextLen = packet_.getSize();
   size_t cipherTextLen = plainTextLen + POLY1305MACLEN;
   BinaryData packetEnc(cipherTextLen);

   int rc = conn->assemblePacket(packet_.getPtr(), plainTextLen, packetEnc.getPtr(), cipherTextLen);
   if (rc != 0) {
      //failed to encrypt, abort
      throw std::runtime_error("failed to encrypt packet, aborting");
   }
   packet_ = std::move(packetEnc);

   return *this;
}

BinaryData MessageBuilder::build() const
{
   return packet_;
}

Message Message::parse(const BinaryDataRef &packet)
{
   try {
      BinaryRefReader reader(packet);
      uint32_t packetLen = reader.get_uint32_t();
      if (packetLen != reader.getSizeRemaining()) {
         return {};
      }
      const auto type = static_cast<ArmoryAEAD::BIP151_PayloadType>(reader.get_uint8_t());
      switch (type)
      {
      case ArmoryAEAD::BIP151_PayloadType::SinglePacket:
         break;

      case ArmoryAEAD::BIP151_PayloadType::Start:
      case ArmoryAEAD::BIP151_PayloadType::PresentPubKey:
      case ArmoryAEAD::BIP151_PayloadType::PresentPubKeyChild:
      case ArmoryAEAD::BIP151_PayloadType::EncInit:
      case ArmoryAEAD::BIP151_PayloadType::EncAck:
      case ArmoryAEAD::BIP151_PayloadType::Rekey:
      case ArmoryAEAD::BIP151_PayloadType::Challenge:
      case ArmoryAEAD::BIP151_PayloadType::Reply:
      case ArmoryAEAD::BIP151_PayloadType::Propose:
         break;

      default:
         return {};
      }

      Message result;
      result.type_ = type;
      result.data_ = reader.get_BinaryDataRef(static_cast<uint32_t>(reader.getSizeRemaining()));
      return result;
   } catch (...) {
      return {};
   }
}

bool Message::isForAEADHandshake() const
{
   return (type_ > ArmoryAEAD::BIP151_PayloadType::Threshold_Begin)
      && (type_ < ArmoryAEAD::BIP151_PayloadType::Threshold_End);
}

ArmoryAEAD::BIP151_PayloadType Message::getMsgType() const
{
   if (isForAEADHandshake())
      throw std::runtime_error("msg is for AEAD sequence");

   return type_;
}

ArmoryAEAD::BIP151_PayloadType Message::getAEADType() const
{
   if (!isForAEADHandshake())
      throw std::runtime_error("msg is not for AEAD sequence");

   return type_;
}
