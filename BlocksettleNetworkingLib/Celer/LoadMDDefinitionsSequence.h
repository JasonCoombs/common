/*

***********************************************************************************
* Copyright (C) 2020 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __CELER_LOAD_DEFINITIONS_FROM_MD_SERVER_H__
#define __CELER_LOAD_DEFINITIONS_FROM_MD_SERVER_H__

#include "CommandSequence.h"

#include <string>
#include <memory>

namespace spdlog
{
   class logger;
}

namespace bs {
   namespace celer {
      class LoadMDDefinitionsSequence : public CommandSequence<LoadMDDefinitionsSequence>
      {
      public:
         LoadMDDefinitionsSequence(const std::shared_ptr<spdlog::logger>& logger);
         ~LoadMDDefinitionsSequence() noexcept override = default;

         LoadMDDefinitionsSequence(const LoadMDDefinitionsSequence&) = delete;
         LoadMDDefinitionsSequence& operator = (const LoadMDDefinitionsSequence&) = delete;

         LoadMDDefinitionsSequence(LoadMDDefinitionsSequence&&) = delete;
         LoadMDDefinitionsSequence& operator = (LoadMDDefinitionsSequence&&) = delete;

         bool FinishSequence() override;

      private:
         CelerMessage sendRequest();

         bool processResponse(const CelerMessage& message);

      private:
         std::shared_ptr<spdlog::logger>  logger_;
      };

   }  //namespace celer
}  //namespace bs

#endif // __CELER_LOAD_DEFINITIONS_FROM_MD_SERVER_H__