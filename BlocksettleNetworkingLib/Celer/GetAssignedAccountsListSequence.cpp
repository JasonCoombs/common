/*

***********************************************************************************
* Copyright (C) 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "GetAssignedAccountsListSequence.h"

#include <spdlog/logger.h>

#include "UpstreamUserAccountProto.pb.h"
#include "DownstreamUserAccountProto.pb.h"
using namespace com::celertech::staticdata::api::user::account;

#include "NettyCommunication.pb.h"
using namespace com::celertech::baseserver::communication::protobuf;
using namespace bs::celer;

GetAssignedAccountsListSequence::GetAssignedAccountsListSequence(const std::shared_ptr<spdlog::logger>& logger
      , const onGetAccountListFunc& cb)
: CommandSequence("CelerGetAssignedAccountsListSequence",
      {
         { false, nullptr, &GetAssignedAccountsListSequence::sendFindAccountRequest}
       , { true, &GetAssignedAccountsListSequence::processFindAccountResponse, nullptr}
      })
 , logger_(logger)
 , cb_(cb)
{}

bool GetAssignedAccountsListSequence::FinishSequence()
{
   if (cb_) {
      cb_(assignedAccounts_);
   }

   return true;
}

CelerMessage GetAssignedAccountsListSequence::sendFindAccountRequest()
{
   FindAssignedUserAccounts request;
   request.set_clientrequestid(GetSequenceId());

   CelerMessage message;
   message.messageType = CelerAPI::FindAssignedUserAccountsType;
   message.messageData = request.SerializeAsString();

   return message;
}

bool GetAssignedAccountsListSequence::processFindAccountResponse(const CelerMessage& message)
{
   if (message.messageType != CelerAPI::MultiResponseMessageType) {
      logger_->error("[CelerGetAssignedAccountsListSequence::processFindAccountResponse] invalid response type {}", message.messageType);
      return false;
   }

   MultiResponseMessage response;
   if (!response.ParseFromString(message.messageData)) {
      logger_->error("[CelerGetAssignedAccountsListSequence::processFindAccountResponse] failed to parse MultiResponseMessage");
      return false;
   }

   for (int i=0; i < response.payload_size(); ++i) {
      const ResponsePayload& payload = response.payload(i);
      auto payloadType = CelerAPI::GetMessageType(payload.classname());
      if (payloadType != CelerAPI::UserAccountDownstreamEventType) {
         logger_->error("[CelerGetAssignedAccountsListSequence::processFindAccountResponse] invalid payload type {}", payload.classname());
         return false;
      }

      UserAccountDownstreamEvent account;
      if (!account.ParseFromString(payload.contents())) {
         logger_->error("[CelerGetAssignedAccountsListSequence::processFindAccountResponse] failed to parse UserAccountDownstreamEvent");
         return false;
      }

      assignedAccounts_.emplace_back(account.account().code());
   }

   return true;
}
