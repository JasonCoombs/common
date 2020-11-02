#ifndef __FUTURES_DEFINITIONS_H__
#define __FUTURES_DEFINITIONS_H__

#include <string>
#include <vector>

#include "CommonTypes.h"

namespace bs {
   namespace network {

      struct FutureDefinitionInfo
      {
         Asset::Type underlyingType;

         std::string ccyPair;

         bool isValid() const;
      };

      FutureDefinitionInfo getFutureDefinition(const std::string& future);
   }
}

#endif // __FUTURES_DEFINITIONS_H__
