/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "StringUtils.h"

#include <algorithm>
#include <cctype>

namespace {

   const char HexUpper[16] = {
      '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
      'A', 'B', 'C', 'D', 'E', 'F' };

   const char HexLower[16] = {
      '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
      'a', 'b', 'c', 'd', 'e', 'f' };

} // namespace

namespace bs {

   // Copied from Botan
   std::string toHex(const std::string &str, bool uppercase)
   {
      const char* tbl = uppercase ? HexUpper : HexLower;

      std::string result;
      result.reserve(str.length() * 2);
      for (size_t i = 0; i < str.length(); ++i) {
         auto x = uint8_t(str[i]);
         result.push_back(tbl[(x >> 4) & 0x0F]);
         result.push_back(tbl[(x) & 0x0F]);
      }

      return result;
   }

   std::string toLower(std::string str)
   {
      std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) {
         return ::tolower(c);
      });
      return str;
   }

   std::string toUpper(std::string str)
   {
      std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) {
         return ::toupper(c);
      });
      return str;
   }

   bool isValidEmail(const std::string &str)
   {
      auto at = std::find(str.begin(), str.end(), '@');
      auto dot = std::find(at, str.end(), '.');
      return (at != str.end()) && (dot != str.end());
   }

   int convertToInt(const std::string& str, bool& converted)
   {
      converted = false;
      try {
         int result = std::stoi(str);
         converted = true;
         return result;
      } catch (...) {
         return 0;
      }
   }

   bool convertToBool(const std::string& str, bool& converted)
   {
      auto inStr = toLower(str);

      if (inStr == "false") {
         converted = true;
         return false;
      }

      if (inStr == "true") {
         converted = true;
         return true;
      }

      return convertToInt(inStr, converted) != 0;
   }

} // namespace bs
