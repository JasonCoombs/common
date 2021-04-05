/*

***********************************************************************************
* Copyright (C) 2020 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __CELER_SIGN_TX_H__
#define __CELER_SIGN_TX_H__

#include "CommandSequence.h"
#include "CommonTypes.h"
#include <string>
#include <functional>
#include <memory>

namespace spdlog {
   class logger;
}

namespace bs {
   namespace celer {

      class SignTxSequence : public CommandSequence<SignTxSequence>
      {
      public:
         SignTxSequence(const QString &orderId, const std::string &txData
            , const std::shared_ptr<spdlog::logger>&);
         ~SignTxSequence() noexcept = default;

         SignTxSequence(const SignTxSequence&) = delete;
         SignTxSequence& operator = (const SignTxSequence&) = delete;
         SignTxSequence(SignTxSequence&&) = delete;
         SignTxSequence& operator = (SignTxSequence&&) = delete;

         bool FinishSequence() override { return true; }

      private:
         CelerMessage send();

         QString           orderId_;
         const std::string txData_;
         std::shared_ptr<spdlog::logger> logger_;
      };

   }  //namespace celer
}  //namespace bs

#endif // __CELER_SIGN_TX_H__
