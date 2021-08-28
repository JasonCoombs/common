/*

***********************************************************************************
* Copyright (C) 2020 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __RAW_CLIENT_CONNECTION_H__
#define __RAW_CLIENT_CONNECTION_H__

#include <spdlog/spdlog.h>
#include <string>
#include <memory>

#include "ActiveStreamClient.h"
#include "ZmqStreamServerConnection.h"

namespace bs {
   namespace network {
      template<class _S>
      class ClientConnection : public _S
      {
      public:
         ClientConnection(const std::shared_ptr<spdlog::logger>& logger)
            : _S(logger)
            , pendingDataSize_(0)
         {}

         ~ClientConnection() noexcept = default;

         ClientConnection(const ClientConnection&) = delete;
         ClientConnection& operator = (const ClientConnection&) = delete;

         ClientConnection(ClientConnection&&) = delete;
         ClientConnection& operator = (ClientConnection&&) = delete;

      public:
         bool send(const std::string& data) override
         {
            auto size = data.size();
            char sizeBuffer[4];
            uint32_t bufferLength = 0;

            while (true) {
               if (bufferLength == 4) {
                  return false;
               }

               sizeBuffer[bufferLength] = size & 0x7f;
               size = size >> 7;
               if (size == 0) {
                  break;
               }

               sizeBuffer[bufferLength] |= 0x80;
               ++bufferLength;
            }
            return _S::sendRawData(std::string((char*)sizeBuffer, bufferLength + 1) + data);
         }

      protected:
         void onRawDataReceived(const std::string& rawData) override
         {
            pendingData_.append(rawData);

            while (!pendingData_.empty()) {
               if (pendingDataSize_ == 0) {
                  const char* sizeBuffer = pendingData_.c_str();

                  int offset = 0;
                  int sizeBytesCount = 0;

                  while (true) {
                     if (offset >= pendingData_.size()) {
                        _S::logger_->error("[ClientConnection] not all size bytes received");
                        return;
                     }

                     if (sizeBuffer[offset] & 0x80) {
                        if (offset == 3) {
                           // we do not expect more than 4 bytes for size
                           _S::logger_->error("[ClientConnection] could not decode size");
                           return;
                        }

                        offset += 1;
                     } else {
                        break;
                     }
                  }

                  sizeBytesCount = offset + 1;

                  while (offset >= 0) {
                     pendingDataSize_ = pendingDataSize_ << 7;
                     pendingDataSize_ |= (sizeBuffer[offset] & 0x7f);
                     offset -= 1;
                  }

                  pendingData_ = pendingData_.substr(sizeBytesCount);
               }

               if (pendingDataSize_ > pendingData_.size()) {
                  break;
               }

               _S::notifyOnData(pendingData_.substr(0, pendingDataSize_));
               pendingData_ = pendingData_.substr(pendingDataSize_);
               pendingDataSize_ = 0;
            }
         }

      private:
         size_t      pendingDataSize_;
         std::string pendingData_;
      };


      class StreamServerConnection : public ZmqStreamServerConnection
      {
      public:
         StreamServerConnection(const std::shared_ptr<spdlog::logger>& logger
            , const std::shared_ptr<ZmqContext>& context)
            : ZmqStreamServerConnection(logger, context)
         {}

        ~StreamServerConnection() noexcept = default;

        StreamServerConnection(const StreamServerConnection&) = delete;
        StreamServerConnection& operator = (const StreamServerConnection&) = delete;
        StreamServerConnection(StreamServerConnection&&) = delete;
        StreamServerConnection& operator = (StreamServerConnection&&) = delete;

      protected:
         server_connection_ptr CreateActiveConnection() override;
      };


      std::shared_ptr<ServerConnection> createServerConnection(const std::shared_ptr<spdlog::logger>& logger
         , const std::shared_ptr<ZmqContext>& zmqContext);

   }  //namespace network
}  //namespace bs

#endif // __RAW_CLIENT_CONNECTION_H__
