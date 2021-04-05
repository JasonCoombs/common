/*

***********************************************************************************
* Copyright (C) 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "BIP15xMessage.h"
#include "BIP15x_Handshake.h"

using namespace bs::network::bip15x;

void MessageBuilder::construct(const uint8_t *data, uint32_t dataSize, MsgType type)
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

MessageBuilder::MessageBuilder(const BinaryDataRef &data, MsgType type)
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
      const uint8_t type = reader.get_uint8_t();
      switch (type)
      {
      case (uint8_t)MsgType::SinglePacket:
         break;

      case ArmoryAEAD::HandshakeSequence::Start:
      case ArmoryAEAD::HandshakeSequence::PresentPubKey:
      case ArmoryAEAD::HandshakeSequence::PresentPubKeyChild:
      case ArmoryAEAD::HandshakeSequence::EncInit:
      case ArmoryAEAD::HandshakeSequence::EncAck:
      case ArmoryAEAD::HandshakeSequence::Rekey:
      case ArmoryAEAD::HandshakeSequence::Challenge:
      case ArmoryAEAD::HandshakeSequence::Reply:
      case ArmoryAEAD::HandshakeSequence::Propose:
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
   return (type_ > ArmoryAEAD::HandshakeSequence::Threshold_Begin)
      && (type_ < ArmoryAEAD::HandshakeSequence::Threshold_End);
}

MsgType Message::getMsgType() const
{
   if (isForAEADHandshake())
      throw std::runtime_error("msg is for AEAD sequence");

   return MsgType(type_);
}

ArmoryAEAD::HandshakeSequence Message::getAEADType() const
{
   if (!isForAEADHandshake())
      throw std::runtime_error("msg is not for AEAD sequence");

   return ArmoryAEAD::HandshakeSequence(type_);
}
