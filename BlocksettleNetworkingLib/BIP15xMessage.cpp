/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "BIP15xMessage.h"

using namespace bs::network::bip15x;

static const uint8_t kPacketPrefix = 0x42;


void MessageBuilder::construct(const uint8_t *data, uint32_t dataSize, MsgType type)
{
   //is this payload carrying a msgid?
   const bool insertMsgId = (type == MsgType::SinglePacket);

   BinaryWriter writer;
   writer.put_uint32_t(0); // Placeholder for required BIP150 field - don't touch

   writer.put_uint8_t(static_cast<uint8_t>(type));

   if (insertMsgId) {
      // we don't use msg ID for now but keep it for possible feature usage
      writer.put_uint32_t(0);
   }
   if (dataSize) {
      writer.put_BinaryData(data, dataSize);
   }
   packet_ = writer.getData();
   uint32_t packetSize = uint32_t(packet_.getSize() - sizeof(uint32_t));
   std::memcpy(packet_.getPtr(), &packetSize, sizeof(packetSize));
}

MessageBuilder::MessageBuilder(const uint8_t *data, uint32_t dataSize, MsgType type)
{
   construct(data, dataSize, type);
}

MessageBuilder::MessageBuilder(const std::vector<uint8_t> &data, MsgType type)
{
   construct(data.data(), data.size(), type);
}

MessageBuilder::MessageBuilder(const BinaryDataRef &data, MsgType type)
{
   construct(data.getPtr(), data.getSize(), type);
}

MessageBuilder::MessageBuilder(const std::string &data, MsgType type)
{
   construct(reinterpret_cast<const uint8_t*>(data.data()), data.size(), type);
}

MessageBuilder::MessageBuilder(MsgType type)
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
   BinaryWriter writer;
   writer.put_uint8_t(kPacketPrefix);
   writer.put_var_int(packet_.getSize());
   writer.put_BinaryData(packet_);
   return writer.getData();
}

std::vector<BinaryData> MessageBuilder::parsePackets(const BinaryDataRef &packet)
{
   try {
      std::vector<BinaryData> result;
      BinaryRefReader reader(packet);

      while (reader.getSizeRemaining() > 0) {
         const uint8_t prefix = reader.get_uint8_t();
         if (prefix != kPacketPrefix) {
            return {};
         }
         const int len = reader.get_var_int();
         if (len > reader.getSizeRemaining()) {
            return { {} }; // Special return type, means "wait for more data"
         }
         result.push_back(reader.get_BinaryData(len));
      }
      return result;
   } catch (const std::exception &) {
      return {};
   }
}

Message Message::parse(const BinaryDataRef &packet)
{
   try {
      BinaryRefReader reader(packet);
      uint32_t packetLen = reader.get_uint32_t();
      if (packetLen != reader.getSizeRemaining()) {
         return {};
      }

      const auto type = static_cast<MsgType>(reader.get_uint8_t());
      switch (type)
      {
      case MsgType::SinglePacket:
         // skip not used message ID
         reader.get_uint32_t();
         break;

      case MsgType::AEAD_Setup:
      case MsgType::AEAD_PresentPubkey:
      case MsgType::AEAD_EncInit:
      case MsgType::AEAD_EncAck:
      case MsgType::AEAD_Rekey:
      case MsgType::AuthChallenge:
      case MsgType::AuthReply:
      case MsgType::AuthPropose:
      case MsgType::Heartbeat:
      case MsgType::Disconnect:
         break;

      default:
         return {};
      }

      Message result;
      result.type_ = type;
      result.data_ = reader.get_BinaryDataRef(reader.getSizeRemaining());
      return result;
   } catch (...) {
      return {};
   }
}
