/*

***********************************************************************************
* Copyright (C) 2020 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "Message/Adapter.h"
#include "Message/Bus.h"

using namespace bs::message;

bool Adapter::push(const Envelope &env)
{
   if (!queue_ || !env.id()) {
      return false;
   }
   return queue_->push(env);
}

bool Adapter::pushFill(Envelope &env)
{
   if (!queue_) {
      return false;
   }
   return queue_->pushFill(env);
}

SeqId Adapter::pushRequest(const std::shared_ptr<User>& sender
   , const std::shared_ptr<User>& receiver
   , const std::string& msg, const TimeStamp& execAt)
{
   auto env = Envelope::makeRequest(sender, receiver, msg, execAt);
   pushFill(env);
   return env.id();
}

SeqId Adapter::pushResponse(const std::shared_ptr<User>& sender
   , const std::shared_ptr<User>& receiver
   , const std::string& msg, SeqId respId)
{
   auto env = Envelope::makeResponse(sender, receiver, msg, respId);
   pushFill(env);
   return env.id();
}

SeqId Adapter::pushResponse(const std::shared_ptr<User>& sender
   , const bs::message::Envelope& envReq, const std::string& msg)
{
   auto env = Envelope::makeResponse(sender, envReq.sender, msg, envReq.foreignId());
   pushFill(env);
   return env.id();
}

SeqId Adapter::pushBroadcast(const std::shared_ptr<User>& sender
   , const std::string& msg, bool global)
{
   auto env = Envelope::makeBroadcast(sender, msg, global);
   pushFill(env);
   return env.id();
}


bool PipeAdapter::process(const Envelope &env)
{
   if (!endpoint_) {
      return false;
   }
   return endpoint_->push(env);
}
