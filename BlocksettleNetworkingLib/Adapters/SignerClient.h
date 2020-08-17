/*

***********************************************************************************
* Copyright (C) 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef SIGNER_CLIENT_H
#define SIGNER_CLIENT_H

#include <memory>

namespace spdlog {
   class logger;
}

class SignerClient
{
public:
   SignerClient(const std::shared_ptr<spdlog::logger> &);
   virtual ~SignerClient() = default;

private:

private:
   std::shared_ptr<spdlog::logger>     logger_;
};


#endif	// SIGNER_CLIENT_H
