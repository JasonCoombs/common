/*

***********************************************************************************
* Copyright (C) 2020 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __CELER_CREATE_FX_ORDER_H__
#define __CELER_CREATE_FX_ORDER_H__

#include "CommandSequence.h"
#include "CommonTypes.h"
#include <memory>
#include <string>


namespace spdlog {
   class logger;
}

namespace bs {
   namespace celer {
      class CreateFxOrderSequence : public CommandSequence<CreateFxOrderSequence>
      {
      public:
         CreateFxOrderSequence(const std::string& accountName, const QString& reqId
            , const bs::network::Quote&, const std::shared_ptr<spdlog::logger>&);
         ~CreateFxOrderSequence() noexcept = default;

         CreateFxOrderSequence(const CreateFxOrderSequence&) = delete;
         CreateFxOrderSequence& operator = (const CreateFxOrderSequence&) = delete;
         CreateFxOrderSequence(CreateFxOrderSequence&&) = delete;
         CreateFxOrderSequence& operator = (CreateFxOrderSequence&&) = delete;

         bool FinishSequence() override;

      private:
         CelerMessage createOrder();

         QString              reqId_;
         bs::network::Quote   quote_;
         std::shared_ptr<spdlog::logger> logger_;
         const std::string accountName_;
      };

   }  //namespace celer
}  //namespace bs

#endif // __CELER_CREATE_FX_ORDER_H__
