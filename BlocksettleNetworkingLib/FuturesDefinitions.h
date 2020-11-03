#ifndef __FUTURES_DEFINITIONS_H__
#define __FUTURES_DEFINITIONS_H__

#include <string>
#include <vector>

#include "CommonTypes.h"

namespace bs {
   namespace network {

      struct FutureDefinitionInfo
      {
         // settlementAssetType - which type will be reported to celer, to
         // trigger specific matching and filling strategy
         Asset::Type settlementAssetType;

         // displayAssetType what asset type should be used to display data
         // related to that type on UI
         Asset::Type displayAssetType;

         std::string ccyPair;

         bool isValid() const;
      };

      FutureDefinitionInfo getFutureDefinition(const std::string& future);
   }
}

#endif // __FUTURES_DEFINITIONS_H__
