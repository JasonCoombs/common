/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __CC_PUB_CONNECTION_H__
#define __CC_PUB_CONNECTION_H__

#include "CommonTypes.h"

#include <QObject>

#include <memory>
#include <string>

namespace spdlog {
   class logger;
}

class CCPubConnection : public QObject
{
Q_OBJECT

public:
   CCPubConnection(const std::shared_ptr<spdlog::logger> &);
   ~CCPubConnection() noexcept override = default;

   CCPubConnection(const CCPubConnection&) = delete;
   CCPubConnection& operator = (const CCPubConnection&) = delete;

   CCPubConnection(CCPubConnection&&) = delete;
   CCPubConnection& operator = (CCPubConnection&&) = delete;

protected:
   virtual bool IsTestNet() const = 0;

   virtual void ProcessGenAddressesResponse(const std::string& response, const std::string &sig) = 0;

protected:
   void OnDataReceived(const std::string& data);

   std::shared_ptr<spdlog::logger>  logger_;
   unsigned int                     currentRev_ = 0;

};

#endif // __CC_PUB_CONNECTION_H__
