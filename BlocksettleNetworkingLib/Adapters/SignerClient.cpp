/*

***********************************************************************************
* Copyright (C) 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "SignerClient.h"
#include <spdlog/spdlog.h>


SignerClient::SignerClient(const std::shared_ptr<spdlog::logger> &logger)
   : logger_(logger)
{}
