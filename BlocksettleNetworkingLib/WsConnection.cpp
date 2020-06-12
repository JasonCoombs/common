/*

***********************************************************************************
* Copyright (C) 2020 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/

#include "WsConnection.h"

#include <libwebsockets.h>

using namespace bs::network;

const char *bs::network::kProtocolNameWs = "bs-ws-protocol";

namespace {

   constexpr size_t kLwsPrePaddingSize = LWS_PRE;

}

WsPacket::WsPacket(const std::string &data)
{
   data_.resize(kLwsPrePaddingSize);
   data_.insert(data_.end(), data.begin(), data.end());
}

uint8_t *WsPacket::getPtr()
{
   return data_.data() + kLwsPrePaddingSize;
}

size_t WsPacket::getSize() const
{
   return data_.size() - kLwsPrePaddingSize;
}
