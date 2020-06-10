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

      extern const size_t kLwsPrePaddingSize;

      class WsPacket
      {
      private:
         // Actual data padded by LWS_PRE
         std::vector<uint8_t> data_;

      public:
         explicit WsPacket(const std::string &data);

         uint8_t *getPtr()
         {
            return data_.data() + kLwsPrePaddingSize;
         }

         size_t getSize() const
         {
            return data_.size() - kLwsPrePaddingSize;
         }
      };

      extern const char *kProtocolNameWs;

      const size_t kRxBufferSize = 16 * 1024;
      const size_t kTxPacketSize = 16 * 1024;
      const int kId = 0;

      const auto kPingPongInterval = std::chrono::seconds(30);

   }
}

#endif // WS_CONNECTION_H
