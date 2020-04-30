/*

***********************************************************************************
* Copyright (C) 2016 - , BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef PROTOBUF_HEADLESS_UTILS_H
#define PROTOBUF_HEADLESS_UTILS_H

#include "CoreWallet.h"
#include "headless.pb.h"

using namespace Blocksettle::Communication;

namespace spdlog {
   class logger;
}

namespace bs {
namespace signer {
   headless::SignTxRequest coreTxRequestToPb(const bs::core::wallet::TXSignRequest &txSignReq
      , bool keepDuplicatedRecipients = false);
   bs::core::wallet::TXSignRequest pbTxRequestToCore(const headless::SignTxRequest &request
      , const std::shared_ptr<spdlog::logger> &logger = nullptr);
}
}

#endif // PROTOBUF_HEADLESS_UTILS_H
