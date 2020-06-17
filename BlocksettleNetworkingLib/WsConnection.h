/*

***********************************************************************************
* Copyright (C) 2020 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef WS_CONNECTION_H
#define WS_CONNECTION_H

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace bs {
   namespace network {

      class WsPacket
      {
      private:
         // Actual data padded by LWS_PRE
         std::vector<uint8_t> data_;

      public:
         explicit WsPacket(const std::string &data);

         uint8_t *getPtr();

         size_t getSize() const;
      };

      extern const char *kProtocolNameWs;

      constexpr size_t kRxBufferSize = 16 * 1024;
      constexpr size_t kTxPacketSize = 16 * 1024;
      constexpr int kId = 0;

      constexpr auto kPingPongInterval = std::chrono::seconds(30);

   }
}

#endif // WS_CONNECTION_H
