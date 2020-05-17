/*

***********************************************************************************
* Copyright (C) 2016 - , BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "Bip39.h"
#include <cstdint>
#include <string>
#include <vector>
#include <assert.h>
#include <array>
#include "BtcUtils.h"

namespace {
   constexpr size_t mnemonicWordMult = 3;
   constexpr size_t mnemonicSeedMult = 4;
   constexpr size_t bitsPerMnemonicWord = 11;
   constexpr size_t entropyBitDevisor = 32;
   constexpr size_t hmacIteration = 2048;
   constexpr size_t dictionarySize = 2048;
   constexpr uint8_t byteBits = 8;
   const std::string saltPrefix = "mnemonic";

   inline uint8_t bip39_shift(size_t bit)
   {
      return (1 << (byteBits - (bit % byteBits) - 1));
   }
}

std::vector<std::string> create_mnemonic(const BinaryData& entropy, const std::vector<std::string>& dictionary)
{
   if ((entropy.getSize() % mnemonicSeedMult) != 0)
      return {};

   const size_t entropyBits = (entropy.getSize() * byteBits);
   const size_t checkBits = (entropyBits / entropyBitDevisor);
   const size_t totalBits = (entropyBits + checkBits);
   const size_t wordCount = (totalBits / bitsPerMnemonicWord);

   assert((totalBits % bitsPerMnemonicWord) == 0);
   assert((wordCount % mnemonicWordMult) == 0);

   BinaryData chunk = entropy;
   chunk.append(BtcUtils::getSha256(entropy));

   size_t bit = 0;
   std::vector<std::string> words;

   for (size_t word = 0; word < wordCount; word++)
   {
      size_t position = 0;
      for (size_t loop = 0; loop < bitsPerMnemonicWord; loop++)
      {
         bit = (word * bitsPerMnemonicWord + loop);
         position <<= 1;

         const auto byte = bit / byteBits;

         if ((chunk[byte] & bip39_shift(bit)) > 0)
            position++;
      }

      assert(position < dictionary.size());
      words.push_back(dictionary[position]);
   }

   assert(words.size() == ((bit + 1) / bitsPerMnemonicWord));
   return words;
}

bool validate_mnemonic(const std::vector<std::string> &words, const std::vector<std::string>& dictionary)
{
   const auto wordsCount = words.size();
   if ((wordsCount % mnemonicWordMult) != 0) {
      return false;
   }

   const size_t totalBits = bitsPerMnemonicWord * wordsCount;
   const size_t checkBits = totalBits / (entropyBitDevisor + 1);
   const size_t entropyBits = totalBits - checkBits;

   assert(entropyBits % byteBits == 0);

   BinaryData chunk((totalBits + byteBits - 1) / byteBits);
   size_t globalBit = 0;
   for (const auto& word : words)
   {
      auto wordIter = std::find(dictionary.cbegin(), dictionary.cend(), word);
      if (wordIter == dictionary.cend()) {
         return false;
      }

      const auto position = std::distance(dictionary.cbegin(), wordIter);
      assert(position != -1);

      for (size_t bit = 0; bit < bitsPerMnemonicWord; bit++, globalBit++)
      {
         if (position & (1 << (bitsPerMnemonicWord - bit - 1)))
         {
            const auto byte = globalBit / byteBits;
            chunk[byte] |= bip39_shift(globalBit);
         }
      }
   }

   chunk.resize(entropyBits / byteBits);
   const auto mnemonic = create_mnemonic(chunk, dictionary);
   return std::equal(mnemonic.begin(), mnemonic.end(), words.begin());
}

bool pkcs5_pbkdf2(const SecureBinaryData& passphrase, const BinaryData& salt,
   SecureBinaryData& key, size_t iterations)
{
   BinaryData asalt = salt;
   SecureBinaryData buffer(64U);
   SecureBinaryData digest1(64U);
   SecureBinaryData digest2(64U);

   size_t keyLength = key.getSize();
   BinaryWriter packer(keyLength);
   for (size_t count = 1; keyLength > 0; count++)
   {
      asalt.append((count >> 24) & 0xff);
      asalt.append((count >> 16) & 0xff);
      asalt.append((count >> 8) & 0xff);
      asalt.append(count & 0xff);
      BtcUtils::getHMAC512(passphrase.getPtr(), passphrase.getSize(),
         asalt.getPtr(), asalt.getSize(), digest1.getPtr());
      buffer = digest1;

      for (size_t iteration = 1; iteration < iterations; iteration++)
      {
         BtcUtils::getHMAC512(passphrase.getPtr(), passphrase.getSize(), digest1.getPtr(), digest1.getSize(),
            digest2.getPtr());
         digest1 = digest2;
         for (size_t index = 0; index < buffer.getSize(); index++)
            buffer[index] ^= digest1[index];
      }

      const size_t length = (keyLength < buffer.getSize() ? keyLength : buffer.getSize());
      packer.put_BinaryData(buffer.getSliceRef(0, length));
      keyLength -= length;
   };
   key = packer.getData();

   return true;
}

SecureBinaryData bip39GetSeedFromMnemonic(const std::string& sentence)
{
   const std::string salt(saltPrefix);

   SecureBinaryData result(64);
   const auto success = pkcs5_pbkdf2(SecureBinaryData::fromString(sentence),
      BinaryData::fromString(salt),
      result, hmacIteration);

   return success ? result : SecureBinaryData();
}
