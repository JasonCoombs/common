/*

***********************************************************************************
* Copyright (C) 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "AuthEidAdapter.h"
#include <spdlog/spdlog.h>

#include "common.pb.h"

using namespace BlockSettle::Common;
using namespace bs::message;


AuthEidAdapter::AuthEidAdapter(const std::shared_ptr<spdlog::logger> &logger
   , const std::shared_ptr<bs::message::User> &user)
   : logger_(logger)
   , user_(user)
{}

bool AuthEidAdapter::process(const bs::message::Envelope &env)
{
   //FIXME: this is just a temporary stub, put loading bc to more appropriate place
/*   OnChainTrackMessage msg;
   msg.mutable_loading();
   Envelope envBC{ 0, user_, nullptr, {}, {}, msg.SerializeAsString() };
   pushFill(envBC);*/
   return true;
}
