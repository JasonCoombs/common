/*

***********************************************************************************
* Copyright (C) 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __CELER_SET_USER_PROPERTY_SEQUENCE_H__
#define __CELER_SET_USER_PROPERTY_SEQUENCE_H__

#include <string>
#include <functional>
#include <memory>
#include "CommandSequence.h"
#include "Property.h"

namespace spdlog {
   class logger;
}

namespace bs {
   namespace celer {
      class SetUserPropertySequence : public CommandSequence<SetUserPropertySequence>
      {
      public:
         using callback_func = std::function<void(bool)>;

         SetUserPropertySequence(const std::shared_ptr<spdlog::logger>&
            , const std::string& username, const Property&);
         ~SetUserPropertySequence() noexcept = default;

         SetUserPropertySequence(const SetUserPropertySequence&) = delete;
         SetUserPropertySequence& operator = (const SetUserPropertySequence&) = delete;

         SetUserPropertySequence(SetUserPropertySequence&&) = delete;
         SetUserPropertySequence& operator = (SetUserPropertySequence&&) = delete;

         void SetCallback(const callback_func& callback) {
            callback_ = callback;
         }

         bool FinishSequence() override {
            if (callback_) {
               callback_(result_);
            }
            return result_;
         }

      private:
         CelerMessage sendSetPropertyRequest();

         bool processSetPropertyResponse(const CelerMessage& message);

      private:
         std::shared_ptr<spdlog::logger> logger_;

         std::string    userName_;
         Property  property_;

         bool           result_;
         callback_func  callback_;
      };

   }  //namespace celer
}  //namespace bs

#endif // __CELER_SET_USER_PROPERTY_SEQUENCE_H__
