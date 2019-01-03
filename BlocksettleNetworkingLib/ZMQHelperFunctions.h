#ifndef __ZMQ_HELPER_FUNCTIONS_H__
#define __ZMQ_HELPER_FUNCTIONS_H__

#include <string>
#include <spdlog/spdlog.h>
#include "EncryptionUtils.h"

#define CURVEZMQPUBKEYBUFFERSIZE 41
#define CURVEZMQPRVKEYBUFFERSIZE 41

namespace bs
{
   namespace network
   {
      int get_monitor_event(void *monitor);
      int get_monitor_event(void *monitor, int *value);
      std::string peerAddressString(int socket);
      int getCurveZMQKeyPair(std::pair<BinaryData, SecureBinaryData>& keyPair);
   }
}

#endif // __ZMQ_HELPER_FUNCTIONS_H__
