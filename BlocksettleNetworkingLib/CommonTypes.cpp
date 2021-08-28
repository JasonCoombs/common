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

Q_DECLARE_METATYPE(bs::PayoutSignatureType)

using namespace bs::network;

class CommonTypesMetaRegistration
{
public:
   CommonTypesMetaRegistration()
   {
      qRegisterMetaType<bs::network::Asset::Type>("AssetType");
      qRegisterMetaType<bs::network::Quote>("Quote");
      qRegisterMetaType<bs::network::Order>("Order");
      qRegisterMetaType<bs::network::SecurityDef>("SecurityDef");
      qRegisterMetaType<bs::network::QuoteReqNotification>("QuoteReqNotification");
      qRegisterMetaType<bs::network::QuoteNotification>("QuoteNotification");
      qRegisterMetaType<bs::network::MDField>("MDField");
      qRegisterMetaType<bs::network::MDFields>("MDFields");
      qRegisterMetaType<bs::network::CCSecurityDef>("CCSecurityDef");
      qRegisterMetaType<bs::network::NewTrade>("NewTrade");
      qRegisterMetaType<bs::network::NewPMTrade>("NewPMTrade");
      qRegisterMetaType<bs::network::UnsignedPayinData>();
      qRegisterMetaType<bs::PayoutSignatureType>();
   }

   ~CommonTypesMetaRegistration() noexcept = default;

   CommonTypesMetaRegistration(const CommonTypesMetaRegistration&) = delete;
   CommonTypesMetaRegistration& operator = (const CommonTypesMetaRegistration&) = delete;

   CommonTypesMetaRegistration(CommonTypesMetaRegistration&&) = delete;
   CommonTypesMetaRegistration& operator = (CommonTypesMetaRegistration&&) = delete;
};

static CommonTypesMetaRegistration commonTypesRegistrator{};

bool RFQ::isXbtBuy() const
{
   if (assetType != Asset::SpotXBT) {
      return false;
   }
   return (product != XbtCurrency) ? (side == Side::Sell) : (side == Side::Buy);
}


QuoteNotification::QuoteNotification(const QuoteReqNotification &qrn
   , const std::string &_authKey, double prc, const std::string &txData)
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
   return { MDField::Unknown, 0, QString() };
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
   return levelQuantity == QStringLiteral("1");
}


const char *Side::toString(Side::Type side)
{
   switch (side) {
      case Buy:   return QT_TR_NOOP("BUY");
      case Sell:  return QT_TR_NOOP("SELL");
      default:    return "unknown";
   }
}

const char *Side::responseToString(Side::Type side)
{
   switch (side) {
      case Buy:   return QT_TR_NOOP("Offer");
      case Sell:  return QT_TR_NOOP("Bid");
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
   case bs::fut::Product::DelvXbtEur:  return QT_TR_NOOP("XBT/EUR 1-day deliverable");
   case bs::fut::Product::RollXbtEur:  return QT_TR_NOOP("XBT/EUR 1-day rolling");
   case bs::fut::Product::DelvXbtUsd:  return QT_TR_NOOP("XBT/USD 1-day deliverable");
   case bs::fut::Product::RollXbtUsd:  return QT_TR_NOOP("XBT/USD 1-day rolling");
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
   throw std::invalid_argument("unknown product " + pt);
   return bs::fut::Product::Undefined;
}

bs::fut::Product bs::fut::fromGenoaProductType(const std::string& product)
{
   if (product == "XBT/EURD") { return bs::fut::Product::DelvXbtEur; }
   if (product == "XBT/EURP") { return bs::fut::Product::RollXbtEur; }
   if (product == "XBT/USDD") { return bs::fut::Product::DelvXbtUsd; }
   if (product == "XBT/USDP") { return bs::fut::Product::RollXbtUsd; }
   throw std::invalid_argument("invalid product " + product);
   return bs::fut::Product::Undefined;
}


const char *bs::network::Asset::toString(bs::network::Asset::Type at)
{
   switch (at) {
   case SpotFX:               return QT_TR_NOOP("Spot FX");
   case SpotXBT:              return QT_TR_NOOP("Spot XBT");
   case PrivateMarket:        return QT_TR_NOOP("Private Market");
   case Future:               return QT_TR_NOOP("Future");
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
