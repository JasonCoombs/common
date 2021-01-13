/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __CELER_CREATE_ORDER_H__
#define __CELER_CREATE_ORDER_H__

#include "CommandSequence.h"
#include "CommonTypes.h"
#include <memory>
#include <string>
#include <functional>

namespace spdlog {
   class logger;
}

namespace bs {
   namespace celer {
      class CreateOrderSequence : public CommandSequence<CreateOrderSequence>
      {
      public:
         CreateOrderSequence(const std::string& accountName, const QString& reqId
            , const bs::network::Quote&, const std::string& payoutTx
            , const std::shared_ptr<spdlog::logger>&);

         CreateOrderSequence(const CreateOrderSequence&) = delete;
         CreateOrderSequence& operator = (const CreateOrderSequence&) = delete;

         CreateOrderSequence(CreateOrderSequence&&) = delete;
         CreateOrderSequence& operator = (CreateOrderSequence&&) = delete;

         bool FinishSequence() override;

      private:
         CelerMessage createOrder();

         QString              reqId_;
         bs::network::Quote   quote_;
         std::string          payoutTx_;
         std::shared_ptr<spdlog::logger> logger_;
         const std::string accountName_;
      };

   }  //namespace celer
}  //namespace bs

#endif // __CELER_CREATE_ORDER_H__
