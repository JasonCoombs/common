#include "FuturesDefinitions.h"

#include <map>

static const std::map<std::string, bs::network::FutureDefinitionInfo> definitions = {
   { "XBTEUR1", { bs::network::Asset::SpotFX, bs::network::Asset::SpotXBT, "XBT/EUR"} }
};

bs::network::FutureDefinitionInfo bs::network::getFutureDefinition(const std::string& future)
{
   auto it = definitions.find(future);
   if (it == definitions.end()) {
      return {};
   }

   return it->second;
}

bool bs::network::FutureDefinitionInfo::isValid() const
{
   return !ccyPair.empty();
}
