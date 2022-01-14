/*

***********************************************************************************
* Copyright (C) 2018 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "CommonTypes.h"
#include "TradesVerification.h"

using namespace bs::network;

bool RFQ::isXbtBuy() const
{
   if (assetType != Asset::SpotXBT) {
      return false;
   }
   return (product != XbtCurrency) ? (side == Side::Sell) : (side == Side::Buy);
}


QuoteNotification::QuoteNotification(const QuoteReqNotification& qrn
   , const std::string& _authKey, double prc, const std::string& txData)
   : authKey(_authKey), reqAuthKey(qrn.requestorAuthPublicKey)
   , settlementId(qrn.settlementId), sessionToken(qrn.sessionToken)
   , quoteRequestId(qrn.quoteRequestId), security(qrn.security), product(qrn.product)
   , transactionData(txData), assetType(qrn.assetType), validityInS(120)
{
   side = bs::network::Side::invert(qrn.side);
   price = prc;
   quantity = qrn.quantity;
}


MDField MDField::get(const MDFields &fields, MDField::Type type)
{
   for (const auto &field : fields) {
      if (field.type == type) {
         return field;
      }
   }
   return { MDField::Unknown, 0, {} };
}

bs::network::MDInfo bs::network::MDField::get(const MDFields &fields)
{
   MDInfo mdInfo;
   mdInfo.bidPrice = bs::network::MDField::get(fields, bs::network::MDField::PriceBid).value;
   mdInfo.askPrice = bs::network::MDField::get(fields, bs::network::MDField::PriceOffer).value;
   mdInfo.lastPrice = bs::network::MDField::get(fields, bs::network::MDField::PriceLast).value;
   return mdInfo;
}

bool bs::network::MDField::isIndicativeForFutures() const
{
   // used for bid/offer only
   return levelQuantity == "1";
}


const char *Side::toString(Side::Type side)
{
   switch (side) {
      case Buy:   return "BUY";
      case Sell:  "SELL";
      default:    return "unknown";
   }
}

const char *Side::responseToString(Side::Type side)
{
   switch (side) {
      case Buy:   return "Offer";
      case Sell:  return "Bid";
      default:    return "";
   }
}

Side::Type Side::invert(Side::Type side)
{
   switch (side) {
      case Buy:   return Sell;
      case Sell:  return Buy;
      default:    return side;
   }
}


bool bs::fut::isDeliverable(bs::fut::Product p)
{
   switch (p) {
      case bs::fut::Product::DelvXbtEur:
      case bs::fut::Product::DelvXbtUsd:
         return true;
      default: break;
   }
   return false;
}

const char* bs::fut::toString(bs::fut::Product p)
{
   switch (p) {
   case bs::fut::Product::DelvXbtEur:  return "XBT/EUR 1-day deliverable";
   case bs::fut::Product::RollXbtEur:  return "XBT/EUR 1-day rolling";
   case bs::fut::Product::DelvXbtUsd:  return "XBT/USD 1-day deliverable";
   case bs::fut::Product::RollXbtUsd:  return "XBT/USD 1-day rolling";
   }
   std::invalid_argument("unknown product " + std::to_string((int)p));
}

std::string bs::fut::toProdType(bs::fut::Product p)
{
   switch (p) {
   case bs::fut::Product::DelvXbtEur:  return "xbteur_df";
   case bs::fut::Product::RollXbtEur:  return "xbteur_rf";
   case bs::fut::Product::DelvXbtUsd:  return "xbtusd_df";
   case bs::fut::Product::RollXbtUsd:  return "xbtusd_rf";
   }
   throw std::invalid_argument("unknown product " + std::to_string((int)p));
}

bs::fut::Product bs::fut::fromProdType(const std::string& pt)
{  //only one mapping (above) to rule them all
   for (int i = (int)bs::fut::Product::first; i < (int)bs::fut::Product::last; ++i) {
      const auto p = static_cast<bs::fut::Product>(i);
      if (bs::fut::toProdType(p) == pt) {
         return p;
      }
   }
   return bs::fut::Product::Undefined;
}

const char *bs::network::Asset::toString(bs::network::Asset::Type at)
{
   switch (at) {
   case SpotFX:               return "Spot FX";
   case SpotXBT:              return "Spot XBT";
   case PrivateMarket:        return "Private Market";
   case Future:               return "Future";
   default:                   return "";
   }
}

bool bs::network::Asset::isSpotType(const Type type)
{
   switch(type) {
   case SpotFX:
   case SpotXBT:
   case PrivateMarket:
      return true;
   case Future:
   default:
      return false;
   }
}

bool bs::network::Asset::isFuturesType(const Type type)
{
   return !isSpotType(type);
}

bool bs::network::isTradingEnabled(UserType userType)
{
   switch (userType) {
      case UserType::Market:
      case UserType::Trading:
      case UserType::Dealing:
         return true;
      default:
         return false;
   }
}

void bs::network::MDInfo::merge(const MDInfo &other)
{
   if (other.bidPrice > 0) {
      bidPrice = other.bidPrice;
   }
   if (other.askPrice > 0) {
      askPrice = other.askPrice;
   }
   if (other.lastPrice > 0) {
      lastPrice = other.lastPrice;
   }
}
