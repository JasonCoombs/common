#include "DecimalPriceAmount.h"

bool validate_string(const std::string& inputData)
{
   const char* str = inputData.c_str();
   const auto length = inputData.length();

   if (length == 0) {
      return false;
   }

   const char* end = str + length;
   const char* p = str;

   if (*p =='-') {
      ++p;
   }

   char cValue = 0;
   bool dotPresent = false;

   while (p != end) {
      cValue = *p++;

      if (cValue == '.') {
         if (dotPresent) {
            return false;
         }
         dotPresent = true;
         continue;
      }

      if (cValue < '0' || cValue > '9') {
         return false;
      }
   }

   return true;
}

template<>
bs::XBTAmount XBTDecimalAmount::toXBTAmount() const
{
   return bs::XBTAmount{static_cast<bs::XBTAmount::satoshi_type>(significand_)};
}
