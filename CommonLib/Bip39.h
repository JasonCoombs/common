/*

***********************************************************************************
* Copyright (C) 2016 - , BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __BS_BIP39_H_
#define __BS_BIP39_H_

#include "BinaryData.h"

// Create mnemonic sentence from entropy and dictionary
std::vector<std::string> create_mnemonic(const BinaryData& entropy,
   const std::vector<std::string>& dictionary);

// validate mnemonic words against particular dictionary
bool validate_mnemonic(const std::vector<std::string> &words,
   const std::vector<std::string>& dictionary);

// validate mnemonic words against list of dictionaries
bool validate_mnemonic(const std::vector<std::string> &words,
   const std::vector<std::vector<std::string>>& dictionaries);

// pbkdf2 algorithm to get bip39 root seed without passphrase
bool pkcs5_pbkdf2(const uint8_t* passphrase, size_t passphrase_length,
   const uint8_t* salt, size_t salt_length, uint8_t* key, size_t key_length,
   size_t iterations);

// return bip39 root seed which could be converted to bip32 root key
SecureBinaryData bip39GetSeedFromMnemonic(const std::string& sentence);

#endif // __BS_BIP39_H_
