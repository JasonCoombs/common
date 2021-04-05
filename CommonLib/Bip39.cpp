/**
 * Copyright (c) 2011-2019 libbitcoin developers (see AUTHORS)
 *
 * This file is part of libbitcoin.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*

***********************************************************************************
* Copyright (C) 2020, BlockSettle AB
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
#include <iterator>

namespace {
   constexpr size_t kMnemonicWordMult = 3;
   constexpr size_t kMnemonicSeedMult = 4;
   constexpr size_t kBitsPerMnemonicWord = 11;
   constexpr size_t kEntropyBitDevisor = 32;
   constexpr size_t kHmacIteration = 2048;
   constexpr size_t kDictionarySize = 2048;
   constexpr uint8_t kByteBits = 8;
   constexpr size_t kElectrumSentenceLength = 12;
   const std::string kBip39SaltPrefix = "mnemonic";
   const std::string kElectrumSaltPrefix = "electrum";
   const std::string kElectrumSeedPrefix = "Seed version";
   
   // https://electrum.readthedocs.io/en/latest/seedphrase.html?highlight=bip39
   const std::set<std::string> kElectrumPrefixes = {
      "01",  // Standard
      "100", // Segwit
      // We do not support two-factor authenticated wallets
      // "101", // Two-factor authenticated wallets standard
      // "102"  // Two-factor authenticated wallets segwit
   };

   inline uint8_t bip39_shift(size_t bit)
   {
      return (1 << (kByteBits - (bit % kByteBits) - 1));
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
      for (size_t count = 1; keyLength > 0; count++) {
         asalt.append((count >> 24) & 0xff);
         asalt.append((count >> 16) & 0xff);
         asalt.append((count >> 8) & 0xff);
         asalt.append(count & 0xff);
         BtcUtils::getHMAC512(passphrase.getPtr(), passphrase.getSize(),
            asalt.getPtr(), asalt.getSize(), digest1.getPtr());
         buffer = digest1;

         for (size_t iteration = 1; iteration < iterations; iteration++) {
            BtcUtils::getHMAC512(passphrase.getPtr(), passphrase.getSize(), digest1.getPtr(), digest1.getSize(),
               digest2.getPtr());
            digest1 = digest2;
            for (size_t index = 0; index < buffer.getSize(); index++) {
               buffer[index] ^= digest1[index];
            }
         }

         const size_t length = (keyLength < buffer.getSize() ? keyLength : buffer.getSize());
         packer.put_BinaryData(buffer.getSliceRef(0, length));
         keyLength -= length;
      };
      key = packer.getData();

      return true;
   }

   std::vector<std::string> createBip39Mnemonic(const BinaryData& entropy, const std::vector<std::string>& dictionary)
   {
      if ((entropy.getSize() % kMnemonicSeedMult) != 0) {
         return {};
      }

      const size_t entropyBits = (entropy.getSize() * kByteBits);
      const size_t checkBits = (entropyBits / kEntropyBitDevisor);
      const size_t totalBits = (entropyBits + checkBits);
      const size_t wordCount = (totalBits / kBitsPerMnemonicWord);

      assert((totalBits % kBitsPerMnemonicWord) == 0);
      assert((wordCount % kMnemonicWordMult) == 0);

      BinaryData chunk = entropy;
      chunk.append(BtcUtils::getSha256(entropy));

      size_t bit = 0;
      std::vector<std::string> words;

      for (size_t word = 0; word < wordCount; word++) {
         size_t position = 0;
         for (size_t loop = 0; loop < kBitsPerMnemonicWord; loop++) {
            bit = (word * kBitsPerMnemonicWord + loop);
            position <<= 1;

            const auto byte = bit / kByteBits;

            if ((chunk[byte] & bip39_shift(bit)) > 0) {
               position++;
            }
         }

         assert(position < dictionary.size());
         words.push_back(dictionary[position]);
      }

      assert(words.size() == ((bit + 1) / kBitsPerMnemonicWord));
      return words;
   }

   bool validateBip39Mnemonic(const std::vector<std::string> &words, const std::vector<std::string>& dictionary)
   {
      const auto wordsCount = words.size();
      if ((wordsCount % kMnemonicWordMult) != 0) {
         return false;
      }

      const size_t totalBits = kBitsPerMnemonicWord * wordsCount;
      const size_t checkBits = totalBits / (kEntropyBitDevisor + 1);
      const size_t entropyBits = totalBits - checkBits;

      assert(entropyBits % kByteBits == 0);

      BinaryData chunk((totalBits + kByteBits - 1) / kByteBits);
      size_t globalBit = 0;
      for (const auto& word : words) {
         auto wordIter = std::find(dictionary.cbegin(), dictionary.cend(), word);
         if (wordIter == dictionary.cend()) {
            return false;
         }

         const auto position = std::distance(dictionary.cbegin(), wordIter);
         assert(position != -1);

         for (size_t bit = 0; bit < kBitsPerMnemonicWord; bit++, globalBit++) {
            if (position & (1 << (kBitsPerMnemonicWord - bit - 1))) {
               const auto byte = globalBit / kByteBits;
               chunk[byte] |= bip39_shift(globalBit);
            }
         }
      }

      chunk.resize(entropyBits / kByteBits);
      const auto mnemonic = createBip39Mnemonic(chunk, dictionary);
      return std::equal(mnemonic.begin(), mnemonic.end(), words.begin());
   }

   std::vector<std::string> splitMnemonicWords(const std::string& sentence) {
      std::vector<std::string> words;
      std::istringstream iss(sentence);
      std::copy(std::istream_iterator<std::string>(iss),
         std::istream_iterator<std::string>(),
         std::back_inserter(words));

      return words;
   }

   std::string normalize(const std::vector<std::string>& words) {
      std::ostringstream oss;
      std::copy(words.begin(), words.end() - 1,
         std::ostream_iterator<std::string>(oss, " "));
      oss << words.back();

      return oss.str();
   }

   std::string normalize(const std::string& sentence) {
      std::vector<std::string> words = splitMnemonicWords(sentence);
      return normalize(words);
   }

   bool isElectrumKnownPrefix(const std::string& prefix) {
      return kElectrumPrefixes.count(prefix) > 0;
   }
}

bool validateBip39Mnemonic(const std::string& sentence,
   const std::vector<std::vector<std::string>>& dictionaries)
{
   std::vector<std::string> words = splitMnemonicWords(sentence);
   for (auto const &dictionary : dictionaries) {
      if (validateBip39Mnemonic(words, dictionary)) {
         return true;
      }
   }

   return false;
}

bool validateElectrumMnemonic(const std::string& sentence)
{
   const auto words = splitMnemonicWords(sentence);
   if (words.size() != kElectrumSentenceLength) {
      return false;
   }

   std::string normalized = normalize(words);
   const auto hmac = BtcUtils::getHMAC512(SecureBinaryData::fromString(kElectrumSeedPrefix),
      normalized);
   const size_t length = (hmac[0] >> 4) + 2;
   auto prefix = hmac.getSliceRef(0, (length / 2) + 1).toHexStr();
   if (length % 2 != 0) {
      prefix.pop_back();
   }

   return isElectrumKnownPrefix(prefix);
}

bool validateMnemonic(const std::string& sentence,
   const std::vector<std::vector<std::string>>& dictionaries)
{
   // #ElectrumSeedsSupport : verifying seeds against electrum logic is skipping for now
   // Since possible situation that mnemonic sentence could be parsed
   // as bip39 seeds and as electrum seed version in the same time
   // we will avoid to detect wallet in this case.
   // So according to this logic, sentence is valid only if
   // it parsed correctly by one method only
   //return validateBip39Mnemonic(sentence, dictionaries) 
   //   != validateElectrumMnemonic(sentence);

   // For now we do only bip39 validation
   return validateBip39Mnemonic(sentence, dictionaries);
}

SecureBinaryData bip39GetSeedFromMnemonic(const std::string& sentence)
{
   std::string salt;

   // #ElectrumSeedsSupport : salt support skipped for now
   //if (validateElectrumMnemonic(sentence)) {
   //   salt = kElectrumSaltPrefix;
   //}
   //else {
   //   salt = kBip39SaltPrefix;
   //}
   salt = kBip39SaltPrefix;

   SecureBinaryData result(64);
   const auto success = pkcs5_pbkdf2(SecureBinaryData::fromString(normalize(sentence)),
      BinaryData::fromString(salt),
      result, kHmacIteration);

   return success ? result : SecureBinaryData();
}
