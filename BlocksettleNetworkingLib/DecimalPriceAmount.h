#ifndef __DECIMAL_PRICE_AMOUNT_H__
#define __DECIMAL_PRICE_AMOUNT_H__

#include <limits>
#include <math.h>
#include <optional>
#include <string>
#include <type_traits>
#include <QJsonValue>
#include "XBTAmount.h"

//   Define and implement main part of decimal price and amount types
// types separated to enforce "logic" on arithmetic operation and disable
// operations that does not make sense in compile time. FOr example price * price,
// or CC amount * FX price...

//   Since type deduction is a bitch, I could not enforce all of the scenarios, but
// most important are there. ( check TestPriceAmountCleasses.cpp for basic examples)


// All type definitions created with using, ti simplify things. Unfortunately using
// just create a synonym, not a hard type, so amounts with same precision will be a same type
// for compiler and not all "rules" on math could be applied.

// Check the EoF for real definitions. This is just a copy.
// using XBTDecimalAmount = DecimalAmount<int64_t, 8>;
// using FXDecimalAmount = DecimalAmount<int64_t, 6>;
// using CCDecimalAmount = DecimalAmount<int64_t, 0>;

// using XBTPrice = DecimalPrice<int64_t, 2, XBTDecimalAmount, FXDecimalAmount>;
// using FXPrice = DecimalPrice<int64_t, 4, FXDecimalAmount, FXDecimalAmount>;
// using CCPrice = DecimalPrice<int64_t, 6, CCDecimalAmount, XBTDecimalAmount>;

// NOTE: usage in protobufs
// there are corresponding messages that represent storage for decimal types in bs_types.proto
// serialize to proto
//    XBTPrice price;
//    price.SerializeToProto(msg.mutable_xbt_price());

// deserialize from proto
//    auto xbtPrice = XBTPrice::DeserializeFromProto(msg.xbt_price());
// or
//    XBTPrice xbtPrice;
//    xbtPrice = msg.xbt_price();

// NOTE: XBTDecimalAmount create examples:
// from bitcoin value ( double )
//    const auto xbtAmount = XBTDecimalAmount::fromArithmetic(bitcoinAmount);
// from satoshis count ( (u)int64_t)
//    const auto xbtAmount = XBTDecimalAmount::fromRawValue(satoshisAmount);

// validate_string - validate if string can be converted to decimal amount or price
bool validate_string(const std::string& inputData);

// PowerOf10 - helper consexpr function to get 10^x in compile time.
template<int p>
inline constexpr int64_t PowerOf10()
{
   static_assert(p > 0, "You could not ask for negative power of 10 here");
   return 10 * PowerOf10<p-1>();
}

template<>
inline constexpr int64_t PowerOf10<0>()
{
   return 1;
}

// fraction_part  - helper function to quickly get "fraction part" from string
// assuming that switch compiled to a jump table and modern CPU could pre-load
// many branches, pipeline should not be broken and code should be very fast.
//   It does not have general solution, so it has to be specialized for each new
// precision added. Currently there are 0, 2, 4, 6, 8.
template<typename T, int precision>
struct fraction_part
{
   static T get_fraction(const char* p, const size_t& length);
};

template<typename T>
struct fraction_part<T, 0>
{
   static T get_fraction(const char*, const size_t&)
   {
      return 0;
   }
};

template<typename T>
struct fraction_part<T, 2>
{
   static T get_fraction(const char* p, const size_t& length)
   {
      T result{0};

      switch (length) {
      case 0:
         break;
      default:
      case 2: result += (p[1] - '0');        [[fallthrough]];
      case 1: result += (p[0] - '0') * 10;
         break;
      }

      return result;
   }
};

template<typename T>
struct fraction_part<T, 4>
{
   static T get_fraction(const char* p, const size_t& length)
   {
      T result{0};

      switch (length) {
      case 0:
         break;
      default:
      case 4: result += (p[3] - '0');        [[fallthrough]];
      case 3: result += (p[2] - '0') * 10;   [[fallthrough]];
      case 2: result += (p[1] - '0') * 100;  [[fallthrough]];
      case 1: result += (p[0] - '0') * 1000;
         break;
      }

      return result;
   }
};

template<typename T>
struct fraction_part<T, 6>
{
   static T get_fraction(const char* p, const size_t& length)
   {
      T result{0};

      switch (length) {
      case 0:
         break;
      default:
      case 6: result += (p[5] - '0');           [[fallthrough]];
      case 5: result += (p[4] - '0') * 10;      [[fallthrough]];
      case 4: result += (p[3] - '0') * 100;     [[fallthrough]];
      case 3: result += (p[2] - '0') * 1000;    [[fallthrough]];
      case 2: result += (p[1] - '0') * 10000;   [[fallthrough]];
      case 1: result += (p[0] - '0') * 100000;
         break;
      }

      return result;
   }
};

template<typename T>
struct fraction_part<T, 8>
{
   static T get_fraction(const char* p, const size_t& length)
   {
      T result{0};

      switch (length) {
      case 0:
         break;
      default:
      case 8: result += (p[7] - '0');              [[fallthrough]];
      case 7: result += (p[6] - '0') * 10;         [[fallthrough]];
      case 6: result += (p[5] - '0') * 100;        [[fallthrough]];
      case 5: result += (p[4] - '0') * 1000;       [[fallthrough]];
      case 4: result += (p[3] - '0') * 10000;      [[fallthrough]];
      case 3: result += (p[2] - '0') * 100000;     [[fallthrough]];
      case 2: result += (p[1] - '0') * 1000000;    [[fallthrough]];
      case 1: result += (p[0] - '0') * 10000000;
         break;
      }

      return result;
   }
};

// decimal_fixed_serialize, decimal_fixed_deserialize - wrapper struct to get conversion to/from string
template<typename T, int precision>
struct decimal_fixed_deserialize
{
   static_assert(std::is_integral<T>::value, "underlying type should be an integral type");
   static_assert(std::is_signed<T>::value, "underlying type should be signed type");

   static constexpr char digits[10] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};

   static bool get_decimal(const std::string& str, T& result)
   {
      return get_decimal(str.c_str(), str.length(), result);
   }

   static bool get_decimal(const char* str, size_t length, T& result)
   {
      bool negative = false;
      const char* end = str + length;
      const char* p = str;

      if (length == 0) {
         return false;
      }

      result = 0;
      T fraction{0};

      // abs value of min negative INT is always bigger than max positive int
      auto maxValue = std::numeric_limits<T>::max();

      if (*p == '-') {
         negative = true;
         ++p;
      }
      maxValue = maxValue / PowerOf10<precision>();

      int cValue = 0;

      while (p != end) {
         cValue = *p++;

         if (cValue == '.') {
            fraction = fraction_part<T, precision>::get_fraction(p, end-p);
            break;
         }

         if (cValue < '0' || cValue > '9') {
            return false;
         }

         cValue -= '0';

         result *= 10;
         result += cValue;

         if (result >= maxValue) {
            return false;
         }
      }

      result = result * PowerOf10<precision>() + fraction;

      if (negative) {
         result = -result;
      }

      return true;
   }
};

template<typename T, int precision>
struct decimal_fixed_serialize
{
   static std::string get_string(T value)
   {
      static_assert(sizeof(T) <= 8, "Update assumed max length of output string");

      // max string of 64bit integrer in base 10 - 20 symbols. '-' and '.' and '\0'
      constexpr int length = 23;

      if (value == 0) {
         return "0";
      }

      char buffer[length];
      char* point = &buffer[0] + length - precision - 1;

      bool negative = false;
      if (value < 0) {
         negative = true;
         value = -value;
      }

      T wholePart = value / PowerOf10<precision>();
      T fractionPart = value % PowerOf10<precision>();

      char* p = point;

      if (wholePart != 0) {
         while (wholePart != 0) {
            --p;
            *p = wholePart % 10 + '0';
            wholePart /= 10;
         }
      } else {
         --p;
         *p = '0';
      }

      char* end;
      if (fractionPart != 0) {
         *point = '.';

         end = point+1;

         auto multiplier = PowerOf10<precision-1>();

         while (fractionPart != 0) {
            *end = fractionPart / multiplier + '0';
            ++end;
            fractionPart = fractionPart % multiplier;
            multiplier /= 10;
         }

      } else {
         end = point;
      }

      if (negative) {
         --p;
         *p = '-';
      }

      return std::string{p, static_cast<size_t>(end-p)};
   }

   inline static double get_double(T value)
   {
      double result = value;
      return result / PowerOf10<precision>();
   }

};

template<typename T>
struct decimal_fixed_serialize<T, 0>
{
   static std::string get_string(T value)
   {
      return std::to_string(value);
   }

   static double get_double(T value)
   {
      return static_cast<double>(value);
   }
};

// CompareResult - add 3 way compare to template args
template<int l, int r>
struct power_compare
{
   enum CompareResult : int
   {
      // -1 = l < r
      // 0 = l == r
      // 1 = l > r
      value = (l < r ? -1 : ( l > r ? 1 : 0) )
   };
};

// change_decimal_value::fix_precision update value to be represented with a new
// precision. precision, in that case an exponent for base 10
template<typename T
   , int current_precision, int required_precision
   , int change_direction = power_compare<current_precision, required_precision>::value>
struct change_decimal_value
{
   static inline T fix_precision(const T& value);
};

template<typename T, int current_precision, int required_precision>
struct change_decimal_value<T, current_precision, required_precision, -1>
{
   static inline T fix_precision(const T& value)
   {
      return value * PowerOf10<required_precision-current_precision>();
   }
};

template<typename T, int current_precision, int required_precision>
struct change_decimal_value<T, current_precision, required_precision, 0>
{
   static inline T fix_precision(const T& value)
   {
      return value;
   }
};

template<typename T, int current_precision, int required_precision>
struct change_decimal_value<T, current_precision, required_precision, 1>
{
   static inline T fix_precision(const T& value)
   {
      return value / PowerOf10<current_precision-required_precision>();
   }
};

template<typename T, int precision, typename NumAmountType, typename DenomAmountType>
class DecimalPrice
{
public:
   using own_type = DecimalPrice<T, precision, NumAmountType, DenomAmountType>;
   using underlying_type = T;
   static constexpr int precision_value = precision;

   using num_type = NumAmountType;
   using denom_type = DenomAmountType;

protected:
   explicit DecimalPrice(const underlying_type& value) : significand_{value} {}

public:
   DecimalPrice() : significand_{0} {}
   ~DecimalPrice() noexcept = default;

   DecimalPrice(const own_type&) = default;
   DecimalPrice& operator = (const own_type&) = default;

   DecimalPrice(own_type&&) = default;
   DecimalPrice& operator = (own_type&&) = default;

   inline void reset()
   {
      significand_ = 0;
   }

   std::string toString() const
   {
      return decimal_fixed_serialize<underlying_type, precision>::get_string(significand_);
   }

   inline double toDouble() const
   {
      return decimal_fixed_serialize<underlying_type, precision>::get_double(significand_);
   }

   inline underlying_type getRawValue() const
   {
      return significand_;
   }

   static std::optional<own_type> fromString(const std::string& str) noexcept
   {
      underlying_type value;

      if (!decimal_fixed_deserialize<underlying_type, precision>::get_decimal(str, value)) {
         return std::nullopt;
      }

      return std::optional<own_type>{own_type::fromRawValue(value)};
   }

   inline own_type abs() const
   {
      return fromRawValue(std::abs(getRawValue()));
   }

   inline bool isZero() const
   {
      return (significand_ == 0);
   }

   template<class A
      , std::enable_if_t<std::is_arithmetic<A>::value, bool> = true>
   static inline own_type fromArithmetic(const A& sourceValue) noexcept
   {
      return own_type{ static_cast<underlying_type>(sourceValue * PowerOf10<precision>()) };
   }

   template<class A
      , std::enable_if_t<std::is_same<A, underlying_type>::value, bool> = true>
   static inline own_type fromRawValue(const A& sourceValue) noexcept
   {
      return own_type{sourceValue};
   }

   template<typename ProtoPrice>
   void inline SerializeToProto(ProtoPrice* destination) const
   {
      destination->set_price_value(significand_);
   }

   template<typename ProtoPrice>
   [[nodiscard]] static inline own_type DeserializeFromProto(const ProtoPrice& source)
   {
      return own_type{source.price_value()};
   }

   template<typename ProtoPrice
      , std::enable_if_t<std::is_class<ProtoPrice>::value, bool> = true>
   DecimalPrice& operator = (const ProtoPrice& source)
   {
      significand_ = source.price_value();
      return *this;
   }

   inline bool operator == (const own_type &other) const
   {
      return (significand_ == other.significand_);
   }

   inline bool operator != (const own_type &other) const
   {
      return (significand_ != other.significand_);
   }

   inline bool operator > (const own_type& other) const
   {
      return (significand_ > other.significand_);
   }
   inline bool operator>=(const own_type& other) const
   {
      return (significand_ >= other.significand_);
   }

   inline bool operator < (const own_type& other) const
   {
      return (significand_ < other.significand_);
   }
   inline bool operator<=(const own_type& other) const
   {
      return (significand_ <= other.significand_);
   }

   inline bool operator ! () const
   {
      return significand_ == 0;
   }

   static own_type min(const own_type& a, const own_type& b)
   {
      return own_type{ std::min(a.significand_, b.significand_) };
   }

   inline own_type operator+(const own_type& another) const
   {
      return own_type{ significand_ + another.significand_ };
   }

   inline own_type& operator+=(const own_type& another)
   {
      significand_ += another.significand_;
      return *this;
   }

   inline own_type operator-(const own_type& another) const
   {
      return own_type{ significand_ - another.significand_ };
   }

   inline own_type operator-() const
   {
      return own_type{ -significand_ };
   }

   inline own_type& operator-=(const own_type& another)
   {
      significand_ -= another.significand_;
      return *this;
   }

   inline own_type operator * (const own_type& another) const
   {
      typename own_type::underlying_type result = significand_ * another.significand_;

      constexpr int result_power = precision_value * 2;

      result = change_decimal_value<decltype(result)
                     , result_power
                     , precision_value>::fix_precision(result);

      return own_type{result};
   }

   inline own_type operator / (const own_type& another) const
   {
      typename own_type::underlying_type result = significand_;

      result = change_decimal_value<decltype(result)
                        , 0
                        , own_type::precision_value>::fix_precision(result);

      result /= another.significand_;

      return own_type{result};
   }

private:
   underlying_type significand_ = 0;
};

template<typename price_type>
inline typename price_type::denom_type operator * (const typename price_type::num_type& left, const price_type& right)
{
   return right * left;
}

template<typename price_type>
typename price_type::denom_type operator * (const price_type& left, const typename price_type::num_type& right)
{
   // do nothing about overflows for now
   typename price_type::denom_type::underlying_type result = left.getRawValue() * right.getRawValue();

   constexpr int result_power = price_type::precision_value + price_type::num_type::precision_value;

   result = change_decimal_value<decltype(result)
                     , result_power
                     , price_type::denom_type::precision_value>::fix_precision(result);

   return price_type::denom_type::fromRawValue(result);
}

template<typename price_type>
typename price_type::num_type operator / (const typename price_type::denom_type& amount, const price_type& price) noexcept
{
   constexpr int result_power = price_type::denom_type::precision_value - price_type::precision_value;

   typename price_type::num_type::underlying_type result = amount.getRawValue();

   result = change_decimal_value<decltype(result)
                     , result_power
                     , price_type::num_type::precision_value>::fix_precision(result);

   result /= price.getRawValue();

   return price_type::num_type::fromRawValue(result);
}

template<typename T, int precision>
class DecimalAmount
{
public:
   using own_type = DecimalAmount<T, precision>;
   using underlying_type = T;
   static constexpr int precision_value = precision;

protected:
   explicit DecimalAmount(const underlying_type& value) : significand_{value} {}

public:
   DecimalAmount() : significand_{0} {}
   ~DecimalAmount() noexcept = default;

   DecimalAmount(const own_type&) = default;
   DecimalAmount& operator = (const own_type&) = default;

   DecimalAmount(own_type&&) = default;
   DecimalAmount& operator = (own_type&&) = default;

   template<int forced_precision
      , std::enable_if_t<forced_precision < precision_value, bool> = true>
   std::string toString() const
   {
      const underlying_type scaledValue = change_decimal_value<own_type::underlying_type
                     , precision_value
                     , forced_precision>::fix_precision(significand_);

      return decimal_fixed_serialize<underlying_type, forced_precision>::get_string(scaledValue);
   }

   std::string toString() const
   {
      return decimal_fixed_serialize<underlying_type, precision>::get_string(significand_);
   }

   inline double toDouble() const
   {
      return decimal_fixed_serialize<underlying_type, precision>::get_double(significand_);
   }

   inline underlying_type getRawValue() const
   {
      return significand_;
   }

   static std::optional<own_type> fromString(const std::string& str) noexcept
   {
      underlying_type value;

      if (!decimal_fixed_deserialize<underlying_type, precision>::get_decimal(str, value)) {
         return std::nullopt;
      }

      return std::optional<own_type>{own_type::fromRawValue(value)};
   }

   static std::optional<own_type> fromJson(const QJsonValue& v) noexcept
   {
      if (v.isDouble()) {
         return std::optional<own_type>{own_type::fromArithmetic(v.toDouble())};
      }
      return fromString(v.toString().toStdString());
   }

   template<class A
      , std::enable_if_t<std::is_arithmetic<A>::value, bool> = true>
   static inline own_type fromArithmetic(const A& sourceValue) noexcept
   {
      return own_type{ static_cast<underlying_type>(sourceValue * PowerOf10<precision>()) };
   }

   template<typename A
      , std::enable_if_t<std::is_same<A, bs::XBTAmount>::value, bool> = true
      , std::enable_if_t< std::is_same<underlying_type, typename A::satoshi_type>::value, bool> = true>
   static inline own_type fromXbtAmount(const A& amount)
   {
      return own_type{amount.GetValue()};
   }

   bs::XBTAmount toXBTAmount() const;

   template<class A
      , std::enable_if_t<std::is_same<A, underlying_type>::value, bool> = true>
   static inline own_type fromRawValue(const A& sourceValue) noexcept
   {
      return own_type{sourceValue};
   }

   template<typename ProtoAmount>
   inline void SerializeToProto(ProtoAmount* destination) const
   {
      destination->set_amount_value(significand_);
   }

   template<typename ProtoAmount>
   [[nodiscard]] static inline own_type DeserializeFromProto(const ProtoAmount& source)
   {
      return own_type{source.amount_value()};
   }

   inline void reset()
   {
      significand_ = 0;
   }

   static inline own_type min(const own_type& a, const own_type& b)
   {
      return own_type{ std::min(a.significand_, b.significand_) };
   }

   inline own_type abs() const
   {
      return own_type{ std::abs(significand_) };
   }

   inline bool isZero() const
   {
      return (significand_ == 0);
   }

   template<typename ProtoAmount>
   DecimalAmount& operator = (const ProtoAmount& source)
   {
      significand_ = source.amount_value();
      return *this;
   }

   inline own_type operator + (const own_type& another) const
   {
      return own_type{significand_ + another.significand_};
   }

   inline own_type& operator+=(const own_type& another)
   {
      significand_ += another.significand_;
      return *this;
   }

   inline own_type operator - (const own_type& another) const
   {
      return own_type{significand_ - another.significand_};
   }

   inline own_type operator - () const
   {
      return own_type{-significand_};
   }

   inline own_type& operator-=(const own_type& another)
   {
      significand_ -= another.significand_;
      return *this;
   }

   inline bool operator == (const own_type &other) const
   {
      return (significand_ == other.significand_);
   }

   inline bool operator != (const own_type &other) const
   {
      return (significand_ != other.significand_);
   }

   inline bool operator > (const own_type& other) const
   {
      return (significand_ > other.significand_);
   }

   inline bool operator < (const own_type& other) const
   {
      return (significand_ < other.significand_);
   }

   inline bool operator <= (const own_type& other) const
   {
      return (significand_ <= other.significand_);
   }

   inline bool operator >= (const own_type& other) const
   {
      return (significand_ >= other.significand_);
   }

   inline bool operator ! () const
   {
      return significand_ == 0;
   }

private:
   underlying_type significand_ = 0;
};

//==============================================================================
//           boolen operators for decimal and fundamental arithmetic types
//==============================================================================
// NOTE: on template args
// std::enable_if_t<std::is_arithmetic<A>::value, bool> = true - instantiate template
//    only if A type is arithmetic ( integral or floating ).
// std::enable_if_t<std::is_same<D, typename D::own_type>::value, bool> = true -
//    little trick to exclude other types that have toDouble() function. All our
//    decimal types have public own_type alias for self, so we instantiate template
//    for types that have .toDouble() and have own_type defined as self type.

// decimal on the left

template<typename D, typename A
   , std::enable_if_t<std::is_arithmetic<A>::value, bool> = true
   , std::enable_if_t<std::is_same<D, typename D::own_type>::value, bool> = true>
inline bool operator < (const D& d, const A& a)
{
   return d.toDouble() < a;
}

template<typename D, typename A
   , std::enable_if_t<std::is_arithmetic<A>::value, bool> = true
   , std::enable_if_t<std::is_same<D, typename D::own_type>::value, bool> = true>
inline bool operator <= (const D& d, const A& a)
{
   return d.toDouble() <= a;
}

template<typename D, typename A
   , std::enable_if_t<std::is_arithmetic<A>::value, bool> = true
   , std::enable_if_t<std::is_same<D, typename D::own_type>::value, bool> = true>
inline bool operator > (const D& d, const A& a)
{
   return d.toDouble() > a;
}

template<typename D, typename A
   , std::enable_if_t<std::is_arithmetic<A>::value, bool> = true
   , std::enable_if_t<std::is_same<D, typename D::own_type>::value, bool> = true>
inline bool operator >= (const D& d, const A& a)
{
   return d.toDouble() >= a;
}

template<typename D, typename A
   , std::enable_if_t<std::is_arithmetic<A>::value, bool> = true
   , std::enable_if_t<std::is_same<D, typename D::own_type>::value, bool> = true>
inline bool operator == (const D& d, const A& a)
{
   return d.toDouble() == a;
}

template<typename D, typename A
   , std::enable_if_t<std::is_arithmetic<A>::value, bool> = true
   , std::enable_if_t<std::is_same<D, typename D::own_type>::value, bool> = true>
inline bool operator != (const D& d, const A& a)
{
   return d.toDouble() != a;
}

// decimal on the right

template<typename D, typename A
   , std::enable_if_t<std::is_arithmetic<A>::value, bool> = true
   , std::enable_if_t<std::is_same<D, typename D::own_type>::value, bool> = true>
inline bool operator < (const A& a, const D& d)
{
   return a < d.toDouble();
}

template<typename D, typename A
   , std::enable_if_t<std::is_arithmetic<A>::value, bool> = true
   , std::enable_if_t<std::is_same<D, typename D::own_type>::value, bool> = true>
inline bool operator <= (const A& a, const D& d)
{
   return a <= d.toDouble();
}

template<typename D, typename A
   , std::enable_if_t<std::is_arithmetic<A>::value, bool> = true
   , std::enable_if_t<std::is_same<D, typename D::own_type>::value, bool> = true>
inline bool operator > (const A& a, const D& d)
{
   return a > d.toDouble();
}

template<typename D, typename A
   , std::enable_if_t<std::is_arithmetic<A>::value, bool> = true
   , std::enable_if_t<std::is_same<D, typename D::own_type>::value, bool> = true>
inline bool operator >= (const A& a, const D& d)
{
   return a >= d.toDouble();
}

template<typename D, typename A
   , std::enable_if_t<std::is_arithmetic<A>::value, bool> = true
   , std::enable_if_t<std::is_same<D, typename D::own_type>::value, bool> = true>
inline bool operator == (const A& a, const D& d)
{
   return a == d.toDouble();
}

template<typename D, typename A
   , std::enable_if_t<std::is_arithmetic<A>::value, bool> = true
   , std::enable_if_t<std::is_same<D, typename D::own_type>::value, bool> = true>
inline bool operator != (const A& a, const D& d)
{
   return a != d.toDouble();
}

//==============================================================================
//           arithmetic operators for decimal and fundamental arithmetic types
//==============================================================================

// decimal on the left
template<typename D, typename A
   , std::enable_if_t<std::is_arithmetic<A>::value, bool> = true
   , std::enable_if_t<std::is_same<D, typename D::own_type>::value, bool> = true>
inline D operator * (const D& d, const A& a)
{
   return D::fromRawValue(static_cast<typename D::underlying_type>(d.getRawValue() * a));
}

// decimal on the right
template<typename D, typename A
   , std::enable_if_t<std::is_arithmetic<A>::value, bool> = true
   , std::enable_if_t<std::is_same<D, typename D::own_type>::value, bool> = true>
inline D operator * (const A& a, const D& d)
{
   return D::fromRawValue(static_cast<typename D::underlying_type>(d.getRawValue() * a));
}

template<typename D, typename A
   , std::enable_if_t<std::is_arithmetic<A>::value, bool> = true
   , std::enable_if_t<std::is_same<D, typename D::own_type>::value, bool> = true>
inline D operator / (const D& d, const A& a)
{
   return D::fromRawValue(static_cast<typename D::underlying_type>(d.getRawValue() / a));
}

//==============================================================================
//                               alias definitions
//==============================================================================

static_assert(BTCNumericTypes::default_precision == 8, "Invalid precision for XBT amount");

using XBTDecimalAmount = DecimalAmount<int64_t, 8>;
// FXDecimalAmount use 6, genoa use 12
using FXDecimalAmount = DecimalAmount<int64_t, 6>;
using CCDecimalAmount = DecimalAmount<int64_t, 0>;

using XBTPrice = DecimalPrice<int64_t, 2, XBTDecimalAmount, FXDecimalAmount>;
using FXPrice = DecimalPrice<int64_t, 4, FXDecimalAmount, FXDecimalAmount>;
using CCPrice = DecimalPrice<int64_t, 6, CCDecimalAmount, XBTDecimalAmount>;

#endif // __DECIMAL_PRICE_AMOUNT_H__
