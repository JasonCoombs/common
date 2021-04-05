/*

***********************************************************************************
* Copyright (C) 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include <spdlog/spdlog.h>
#include "MDCallbacksQt.h"

void MDCallbacksQt::onMDUpdate(bs::network::Asset::Type type, const std::string &security
   , bs::network::MDFields fields)
{
   emit MDUpdate(type, QString::fromStdString(security), fields);
}

void MDCallbacksQt::onMDSecurityReceived(const std::string &security
   , const bs::network::SecurityDef &sd)
{
   emit MDSecurityReceived(security, sd);
}
