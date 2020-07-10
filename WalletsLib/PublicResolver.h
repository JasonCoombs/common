/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __PUBLIC_RESOLVER_H__
#define __PUBLIC_RESOLVER_H__

#include "Address.h"
#include "BinaryData.h"
#include "Script.h"

#include <string>
#include <map>

namespace bs
{
   class PublicResolver : public ResolverFeed
   {
   public:
      PublicResolver(const std::map<bs::Address, BinaryData> &preimageMap);
      PublicResolver(const std::map<std::string, BinaryData> &preimageMap);

      BinaryData getByVal(const BinaryData &addr) override;
      const SecureBinaryData& getPrivKeyForPubkey(const BinaryData &pk) override;
      void setBip32PathForPubkey(const BinaryData&, const std::vector<uint32_t>&) override;
      std::vector<uint32_t> resolveBip32PathForPubkey(const BinaryData&) override;

   private:
      void addPreimage(const BinaryData &unprefixedAddr, const BinaryData &preimage);

   private:
      std::map<BinaryData, BinaryData>   preimageMap_;
   };
};

#endif // __PUBLIC_RESOLVER_H__
