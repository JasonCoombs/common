/*

***********************************************************************************
* Copyright (C) 2021 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef PRICE_AMOUNT_H
#define PRICE_AMOUNT_H

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

namespace bs {

   template<uint32_t precision>
   class PriceAmount
   {
   public:
      using Self = PriceAmount<precision>;

      PriceAmount()
         : value_{}
      {
      }

      // Amount is truncated toward zero, so PriceAmount<2>(1.1299) is equal to PriceAmount<2>(1.12)
      explicit PriceAmount(double amount)
      {
         value_ = static_cast<int64_t>(std::trunc(amount * scale()));
      }

      std::string to_string() const
      {
         std::ostringstream ss;
         int64_t base = std::abs(value_) / scale();
         int64_t rem = std::abs(value_) % scale();
         if (value_ < 0) {
            ss << '-';
         }
         ss << base << "." << std::setw(precision) << std::setfill('0') << rem;
         return ss.str();
      }

      double to_double() const
      {
         return value_ / static_cast<double>(scale());
      }

      bool operator == (const Self &other) const
      {
         return (value_ == other.value_);
      }
      bool operator != (const Self &other) const
      {
         return (value_ != other.value_);
      }
      bool operator > (const Self &other) const
      {
         return (value_ > other.value_);
      }
      bool operator < (const Self &other) const
      {
         return (value_ < other.value_);
      }

      Self operator + (const Self &other) const
      {
         return PriceAmount(value_ + other.value_);
      }
      Self operator - (const Self &other) const
      {
         return PriceAmount(value_ - other.value_);
      }

   private:
      static constexpr uint32_t scaleHelper(uint32_t value)
      {
          return value == 0 ? 1 : 10 * scaleHelper(value - 1);
      }

      static constexpr uint32_t scale()
      {
         return scaleHelper(precision);
      }

      explicit PriceAmount(int64_t amount);

      int64_t value_;
   };

   using CentAmount = PriceAmount<2>;

}

#endif // PRICE_AMOUNT_H
