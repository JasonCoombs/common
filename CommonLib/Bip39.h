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
