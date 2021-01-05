/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __CELER_PROPERTY_H__
#define __CELER_PROPERTY_H__

#include <string>
#include <unordered_map>

namespace bs {
   namespace celer {
      struct Property
      {
         Property(const std::string& propertyName)
            : name(propertyName)
            , id(-1)
         {}
         Property() : id(-1) {}
         bool empty() const { return name.empty(); }

         std::string name;
         std::string value;
         int64_t     id;
      };

      typedef std::unordered_map<std::string, Property>  Properties;

   }  //namespace celer
}  //namespace bs

#endif // __CELER_PROPERTY_H__
