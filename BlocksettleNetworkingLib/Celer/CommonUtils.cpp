/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "CommonUtils.h"

using namespace bs::network;

Side::Type bs::celer::fromCeler(com::celertech::marketmerchant::api::enums::side::Side side)
{
   switch (side) {
   case com::celertech::marketmerchant::api::enums::side::BUY:    return Side::Buy;
   case com::celertech::marketmerchant::api::enums::side::SELL:   return Side::Sell;
   }
   return Side::Undefined;
}

com::celertech::marketmerchant::api::enums::side::Side bs::celer::toCeler(Side::Type side)
{
   switch (side) {
   case Side::Buy:   return com::celertech::marketmerchant::api::enums::side::BUY;
   case Side::Sell:
   default:    return com::celertech::marketmerchant::api::enums::side::SELL;
   }
}

bs::network::Asset::Type bs::celer::fromCelerProductType(com::celertech::marketdata::api::enums::producttype::ProductType pt)
{
   switch (pt) {
   case com::celertech::marketdata::api::enums::producttype::SPOT:           return bs::network::Asset::SpotFX;
   case com::celertech::marketdata::api::enums::producttype::BITCOIN:        return bs::network::Asset::SpotXBT;
   case com::celertech::marketdata::api::enums::producttype::PRIVATE_SHARE:  return bs::network::Asset::PrivateMarket;
   default: return bs::network::Asset::Undefined;
   }
}

bs::network::Asset::Type bs::celer::fromCelerProductType(com::celertech::marketmerchant::api::enums::producttype::ProductType pt)
{
   switch (pt) {
   case com::celertech::marketmerchant::api::enums::producttype::SPOT:           return bs::network::Asset::SpotFX;
   case com::celertech::marketmerchant::api::enums::producttype::BITCOIN:        return bs::network::Asset::SpotXBT;
   case com::celertech::marketmerchant::api::enums::producttype::PRIVATE_SHARE:  return bs::network::Asset::PrivateMarket;
   default: return bs::network::Asset::Undefined;
   }
}

com::celertech::marketmerchant::api::enums::assettype::AssetType bs::celer::toCeler(bs::network::Asset::Type at)
{
   switch (at) {
   case bs::network::Asset::SpotFX:         return com::celertech::marketmerchant::api::enums::assettype::FX;
   case bs::network::Asset::SpotXBT:        return com::celertech::marketmerchant::api::enums::assettype::CRYPTO;
   case bs::network::Asset::PrivateMarket:  return com::celertech::marketmerchant::api::enums::assettype::CRYPTO;
   default:             return com::celertech::marketmerchant::api::enums::assettype::STRUCTURED_PRODUCT;
   }
}

com::celertech::marketdata::api::enums::assettype::AssetType bs::celer::toCelerMDAssetType(bs::network::Asset::Type at)
{
   switch (at) {
   case bs::network::Asset::SpotFX:         return com::celertech::marketdata::api::enums::assettype::FX;
   case bs::network::Asset::SpotXBT:        [[fallthrough]]
   case bs::network::Asset::PrivateMarket:  [[fallthrough]]
   default:
      return com::celertech::marketdata::api::enums::assettype::CRYPTO;
   }
}

com::celertech::marketmerchant::api::enums::producttype::ProductType bs::celer::toCelerProductType(bs::network::Asset::Type at)
{
   switch (at) {
   case bs::network::Asset::SpotFX:         return com::celertech::marketmerchant::api::enums::producttype::SPOT;
   case bs::network::Asset::SpotXBT:        return com::celertech::marketmerchant::api::enums::producttype::BITCOIN;
   case bs::network::Asset::PrivateMarket:  return com::celertech::marketmerchant::api::enums::producttype::PRIVATE_SHARE;
   default:             return com::celertech::marketmerchant::api::enums::producttype::SPOT;
   }
}

com::celertech::marketdata::api::enums::producttype::ProductType bs::celer::toCelerMDProductType(bs::network::Asset::Type at)
{
   switch (at) {
   case bs::network::Asset::SpotFX:         return com::celertech::marketdata::api::enums::producttype::SPOT;
   case bs::network::Asset::SpotXBT:        return com::celertech::marketdata::api::enums::producttype::BITCOIN;
   case bs::network::Asset::PrivateMarket:  return com::celertech::marketdata::api::enums::producttype::PRIVATE_SHARE;
   default:             return com::celertech::marketdata::api::enums::producttype::SPOT;
   }
}

const char* bs::celer::toCelerSettlementType(bs::network::Asset::Type at)
{
   switch (at) {
   case bs::network::Asset::SpotFX:         return "SPOT";
   case bs::network::Asset::SpotXBT:        return "XBT";
   case bs::network::Asset::PrivateMarket:  return "CC";
   default:       return "";
   }
}

bs::network::Order::Status bs::celer::mapFxOrderStatus(com::celertech::marketmerchant::api::enums::orderstatus::OrderStatus status)
{
   switch (status) {
   case com::celertech::marketmerchant::api::enums::orderstatus::FILLED:   return Order::Filled;
   case com::celertech::marketmerchant::api::enums::orderstatus::REJECTED: return Order::Failed;
   case com::celertech::marketmerchant::api::enums::orderstatus::PENDING_NEW: [[fallthrough]]
   case com::celertech::marketmerchant::api::enums::orderstatus::NEW:      return Order::New;
   default:       return Order::Pending;
   }
}

bs::network::Order::Status bs::celer::mapBtcOrderStatus(com::celertech::marketmerchant::api::enums::orderstatus::OrderStatus status)
{
   switch (status) {
   case com::celertech::marketmerchant::api::enums::orderstatus::FILLED:   return Order::Filled;
   case com::celertech::marketmerchant::api::enums::orderstatus::REJECTED: return Order::Failed;
   case com::celertech::marketmerchant::api::enums::orderstatus::PENDING_NEW: return Order::Pending;
   case com::celertech::marketmerchant::api::enums::orderstatus::NEW:      return Order::New;
   default:       return Order::Pending;
   }
}
