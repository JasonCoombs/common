/*

***********************************************************************************
* Copyright (C) 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __TRANSPORT_H__
#define __TRANSPORT_H__

#include <string>
#include "BinaryData.h"
#include "DataConnectionListener.h"
#include "ServerConnectionListener.h"

namespace bs {
   namespace network {

      class TransportClient
      {
      public:
         virtual ~TransportClient() = default;

         virtual std::string listenThreadName() const = 0;

         virtual long pollTimeoutMS() const = 0;

         virtual bool handshakeTimedOut() const = 0;
         virtual bool connectTimedOut() const = 0;

         virtual void startConnection() = 0;
         virtual void openConnection(const std::string &host, const std::string &port) = 0;
         virtual void closeConnection() = 0;
         virtual void socketConnected() = 0;
         virtual void socketDisconnected() = 0;

         virtual void triggerHeartbeatCheck() = 0;
         virtual bool sendData(const std::string &) = 0;
         virtual void sendDisconnect() = 0;
         virtual void startHandshake() = 0;

         virtual void onRawDataReceived(const std::string &) = 0;

         using SendCb = std::function<bool(const std::string &)>;
         void setSendCb(const SendCb &cb) { sendCb_ = cb; }

         using NotifyDataCb = std::function<void(const std::string &)>;
         void setNotifyDataCb(const NotifyDataCb &cb) { notifyDataCb_ = cb; }

         using SocketErrorCb = std::function<void(DataConnectionListener::DataConnectionError)>;
         void setSocketErrorCb(const SocketErrorCb &cb) { socketErrorCb_ = cb; }

      protected:
         SendCb         sendCb_{ nullptr };
         NotifyDataCb   notifyDataCb_{ nullptr };
         SocketErrorCb  socketErrorCb_{ nullptr };
      };


      class TransportServer
      {
      public:
         virtual ~TransportServer() = default;

         virtual void processIncomingData(const std::string &encData
            , const std::string &clientID, int socket) = 0;
         virtual void periodicCheck() = 0;
         virtual bool sendData(const std::string &clientId, const std::string &data) = 0;

         using ClientErrorCb = std::function<void(const std::string &id, const std::string &err)>;
         using ClientSocketErrorCb = std::function<void(const std::string &clientId
            , ServerConnectionListener::ClientError errorCode, int socket)>;
         void setClientErrorCb(const ClientErrorCb &cb, const ClientSocketErrorCb &cbS)
         {
            clientErrorCb_ = cb;
            clientSocketErrorCb_ = cbS;
         }

         using DataReceivedCb = std::function<void(const std::string &clientId, const std::string &data)>;
         void setDataReceivedCb(const DataReceivedCb &cb) { dataReceivedCb_ = cb; }

         using SendDataCb = std::function<bool(const std::string &clientId, const std::string &data)>;
         void setSendDataCb(const SendDataCb &cb) { sendDataCb_ = cb; }

         using ConnectedCb = std::function<void(const std::string &clientId)>;
         void setConnectedCb(const ConnectedCb &connCb, const ConnectedCb &disconnCb)
         {
            connCb_ = connCb;
            disconnCb_ = disconnCb;
         }

      protected:
         ClientErrorCb        clientErrorCb_{ nullptr };
         ClientSocketErrorCb  clientSocketErrorCb_{ nullptr };
         DataReceivedCb       dataReceivedCb_{ nullptr };
         SendDataCb           sendDataCb_{ nullptr };
         ConnectedCb          connCb_{ nullptr };
         ConnectedCb          disconnCb_{ nullptr };
      };

   }  // namespace network
}  // namespace bs

#endif // __TRANSPORT_H__
