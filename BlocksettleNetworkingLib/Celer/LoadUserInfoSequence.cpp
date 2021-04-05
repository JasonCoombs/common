/*

***********************************************************************************
* Copyright (C) 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "LoadUserInfoSequence.h"

#include "UpstreamUserPropertyProto.pb.h"
#include "DownstreamUserPropertyProto.pb.h"
#include "NettyCommunication.pb.h"

#include "PropertyDefinitions.h"

#include <spdlog/logger.h>
#include <iostream>

using namespace bs::celer;
using namespace com::celertech::staticdata::api::user::property;
using namespace com::celertech::baseserver::communication::protobuf;

LoadUserInfoSequence::LoadUserInfoSequence(const std::shared_ptr<spdlog::logger>& logger
   , const std::string& username
   , const onPropertiesRecvd_func& cb)
 : CommandSequence("CelerLoadUserInfoSequence",
      {
           { false, nullptr, &LoadUserInfoSequence::sendGetUserIdRequest}
         , { true, &LoadUserInfoSequence::processGetPropertyResponse, nullptr}

         , { false, nullptr, &LoadUserInfoSequence::sendGetSubmittedAuthAddressListRequest}
         , { true, &LoadUserInfoSequence::processGetPropertyResponse, nullptr}

         , { false, nullptr, &LoadUserInfoSequence::sendGetSubmittedCCAddressListRequest }
         , { true, &LoadUserInfoSequence::processGetPropertyResponse, nullptr }

         ,{ false, nullptr, &LoadUserInfoSequence::sendGetBitcoinParticipantRequest }
         ,{ true, &LoadUserInfoSequence::processGetPropertyResponse, nullptr }

         ,{ false, nullptr, &LoadUserInfoSequence::sendGetBitcoinDealerRequest }
         ,{ true, &LoadUserInfoSequence::processGetPropertyResponse, nullptr }
       })
 , logger_(logger)
 , cb_(cb)
 , username_(username)
{}

bool LoadUserInfoSequence::FinishSequence()
{
   if (cb_) {
      cb_(properties_);
   }

   return true;
}

CelerMessage LoadUserInfoSequence::sendGetUserIdRequest()
{
   return getPropertyRequest(UserIdPropertyName);
}

CelerMessage LoadUserInfoSequence::sendGetSubmittedAuthAddressListRequest()
{
   return getPropertyRequest(SubmittedBtcAuthAddressListPropertyName);
}

CelerMessage LoadUserInfoSequence::sendGetSubmittedCCAddressListRequest()
{
   return getPropertyRequest(SubmittedCCAddressListPropertyName);
}

CelerMessage LoadUserInfoSequence::sendGetBitcoinParticipantRequest()
{
   return getPropertyRequest(BitcoinParticipantPropertyName);
}

CelerMessage LoadUserInfoSequence::sendGetBitcoinDealerRequest()
{
   return getPropertyRequest(BitcoinDealerPropertyName);
}

CelerMessage LoadUserInfoSequence::getPropertyRequest(const std::string& name)
{
   FindUserPropertyByUsernameAndKey request;
   request.set_username(username_);
   request.set_key(name);
   request.set_clientrequestid(GetSequenceId());

   CelerMessage message;
   message.messageType = CelerAPI::FindUserPropertyByUsernameAndKeyType;
   message.messageData = request.SerializeAsString();

   return message;
}

bool LoadUserInfoSequence::processGetPropertyResponse(const CelerMessage& message)
{
   if (message.messageType != CelerAPI::SingleResponseMessageType) {
      logger_->error("[CelerLoadUserInfoSequence::processGetPropertyResponse] get invalid message type {} instead of {}"
                     , message.messageType, CelerAPI::SingleResponseMessageType);
      return false;
   }

   SingleResponseMessage response;
   if (!response.ParseFromString(message.messageData)) {
      logger_->error("[CelerLoadUserInfoSequence::processGetPropertyResponse] failed to parse massage of type {}", message.messageType);
      return false;
   }

   if (!response.has_payload()) {
      return true;
   }

   auto payloadType = CelerAPI::GetMessageType(response.payload().classname());
   if (payloadType != CelerAPI::UserPropertyDownstreamEventType) {
      logger_->error("[CelerLoadUserInfoSequence::processGetPropertyResponse] unexpected type {} for class {}"
         , payloadType, response.payload().classname());
      return false;
   }

   UserPropertyDownstreamEvent event;
   if (!event.ParseFromString(response.payload().contents())) {
      logger_->error("[CelerLoadUserInfoSequence::processGetPropertyResponse] failed to parse UserPropertyDownstreamEvent");
      return false;
   }

   Property property(event.key());

   property.value = event.value();
   if (event.has_id()) {
      property.id = event.id();
   } else {
      property.id = -1;
   }
   properties_.emplace(event.key(), property);

   return true;
}
