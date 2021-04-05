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
* Copyright (C) 2020 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __BS_BIP39_H_
#define __BS_BIP39_H_

#include "BinaryData.h"

// validate bip39 mnemonic words against list of dictionaries
bool validateBip39Mnemonic(const std::string& sentence,
   const std::vector<std::vector<std::string>>& dictionaries);

// validate electrum mnemonic words against list of dictionaries
bool validateElectrumMnemonic(const std::string& sentence);

// check for bip39 & electrum mnemonic compatibility
bool validateMnemonic(const std::string& sentence,
   const std::vector<std::vector<std::string>>& dictionaries);

// return bip32 root seed which could be converted to bip32 root key
// this is the same for bip39 protocol and for electrum seed generation system
SecureBinaryData bip39GetSeedFromMnemonic(const std::string& sentence);

#endif // __BS_BIP39_H_
