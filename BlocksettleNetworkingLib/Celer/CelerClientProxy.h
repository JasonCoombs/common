/*

***********************************************************************************
* Copyright (C) 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef CELER_CLIENT_PROXY_H
#define CELER_CLIENT_PROXY_H

#include "BaseCelerClient.h"
#include "BsClient.h"

class CelerClientProxy : public CelerClientQt
{
   Q_OBJECT
public:
   CelerClientProxy(const std::shared_ptr<spdlog::logger> &logger, bool userIdRequired = true);
   ~CelerClientProxy() override;

   bool LoginToServer(BsClientQt *client, const std::string& login, const std::string& email);

private:
   void onSendData(CelerAPI::CelerMessageType messageType, const std::string &data) override;

   BsClientQt *client_{};
};

#endif
