/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __COMMON_CELER_UTILS_H__
#define __COMMON_CELER_UTILS_H__

#include "CommonTypes.h"
#include "com/celertech/marketmerchant/api/enums/SideProto.pb.h"
#include "com/celertech/marketmerchant/api/enums/AssetTypeProto.pb.h"
#include "com/celertech/marketmerchant/api/enums/OrderStatusProto.pb.h"
#include "com/celertech/marketmerchant/api/enums/ProductTypeProto.pb.h"
#include "com/celertech/marketmerchant/api/enums/MarketDataEntryTypeProto.pb.h"

#include "com/celertech/marketdata/api/enums/AssetTypeProto.pb.h"
#include "com/celertech/marketdata/api/enums/ProductTypeProto.pb.h"
#include "com/celertech/marketdata/api/enums/MarketDataEntryTypeProto.pb.h"


namespace bs {
   namespace celer {
      bs::network::Side::Type fromCeler(com::celertech::marketmerchant::api::enums::side::Side);
      com::celertech::marketmerchant::api::enums::side::Side toCeler(bs::network::Side::Type side);

      bs::network::Asset::Type fromCelerProductType(com::celertech::marketdata::api::enums::producttype::ProductType);
      bs::network::Asset::Type fromCelerProductType(com::celertech::marketmerchant::api::enums::producttype::ProductType);
      com::celertech::marketmerchant::api::enums::assettype::AssetType toCeler(bs::network::Asset::Type);
      com::celertech::marketdata::api::enums::assettype::AssetType toCelerMDAssetType(bs::network::Asset::Type);
      com::celertech::marketmerchant::api::enums::producttype::ProductType toCelerProductType(bs::network::Asset::Type);
      com::celertech::marketdata::api::enums::producttype::ProductType toCelerMDProductType(bs::network::Asset::Type);
      const char *toCelerSettlementType(bs::network::Asset::Type);

      bs::network::MDField::Type fromCeler(com::celertech::marketdata::api::enums::marketdataentrytype::MarketDataEntryType);

      bs::network::Order::Status mapBtcOrderStatus(com::celertech::marketmerchant::api::enums::orderstatus::OrderStatus status);
      bs::network::Order::Status mapFxOrderStatus(com::celertech::marketmerchant::api::enums::orderstatus::OrderStatus status);

   }  //namespace celer
}  //namespace bs


#endif //__COMMON_CELER_UTILS_H__
