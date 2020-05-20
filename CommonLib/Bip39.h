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
