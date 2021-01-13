/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "CelerClientProxy.h"

#include <spdlog/spdlog.h>

CelerClientProxy::CelerClientProxy(const std::shared_ptr<spdlog::logger> &logger, bool userIdRequired)
   : CelerClientQt (logger, userIdRequired, true)
{
}

CelerClientProxy::~CelerClientProxy() = default;

bool CelerClientProxy::LoginToServer(BsClientQt *client, const std::string &login, const std::string &email)
{
   client_ = client;

   connect(client_, &BsClientQt::celerRecv, this, [this](CelerAPI::CelerMessageType messageType, const std::string &data) {
      recvData(messageType, data);
   });

   connect(client_, &QObject::destroyed, this, [this] {
      client_ = nullptr;
   });

   // Password will be replaced by BsProxy
   bool result = SendLogin(login, email, "");
   return result;
}

void CelerClientProxy::onSendData(CelerAPI::CelerMessageType messageType, const std::string &data)
{
   if (!client_) {
      SPDLOG_LOGGER_ERROR(logger_, "BsClient is not valid");
      return;
   }

   client_->celerSend(messageType, data);
}
