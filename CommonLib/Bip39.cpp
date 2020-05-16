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

bool pkcs5_pbkdf2(const uint8_t* passphrase, size_t passphrase_length,
   const uint8_t* salt, size_t salt_length, uint8_t* key, size_t key_length,
   size_t iterations)
{
   uint8_t* asalt;
   size_t asaltSize;
   size_t count, index, iteration, length;
   uint8_t buffer[64U];
   uint8_t digest1[64U];
   uint8_t digest2[64U];

   /* An iteration count of 0 is equivalent to a count of 1. */
   /* A key_length of 0 is a no-op. */
   /* A salt_length of 0 is perfectly valid. */

   if (salt_length > SIZE_MAX - 4)
      return false;
   asaltSize = salt_length + 4;
   asalt = new uint8_t[asaltSize];
   if (asalt == nullptr)
      return false;

   memcpy(asalt, salt, salt_length);
   for (count = 1; key_length > 0; count++)
   {
      asalt[salt_length + 0] = (count >> 24) & 0xff;
      asalt[salt_length + 1] = (count >> 16) & 0xff;
      asalt[salt_length + 2] = (count >> 8) & 0xff;
      asalt[salt_length + 3] = (count >> 0) & 0xff;
      BtcUtils::getHMAC512(passphrase, passphrase_length, asalt, asaltSize, digest1);
      memcpy(buffer, digest1, sizeof(buffer));

      for (iteration = 1; iteration < iterations; iteration++)
      {
         BtcUtils::getHMAC512(passphrase, passphrase_length, digest1, sizeof(digest1),
            digest2);
         memcpy(digest1, digest2, sizeof(digest1));
         for (index = 0; index < sizeof(buffer); index++)
            buffer[index] ^= digest1[index];
      }

      length = (key_length < sizeof(buffer) ? key_length : sizeof(buffer));
      memcpy(key, buffer, length);
      key += length;
      key_length -= length;
   };

   delete[] asalt;

   return true;
}

SecureBinaryData pib39GetSeedFromMnemonic(std::string sentence)
{
   const std::string salt(saltPrefix);

   std::array<uint8_t, 64> result;
   const auto success = pkcs5_pbkdf2(reinterpret_cast<const uint8_t *>(sentence.data()), sentence.size(),
      reinterpret_cast<const uint8_t *>(salt.data()), salt.size(),
      result.data(), result.size(), hmacIteration);

   return success ? SecureBinaryData(result.data(), result.size()) : SecureBinaryData();
}
