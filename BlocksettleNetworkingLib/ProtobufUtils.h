/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef PROTOBUF_UTILS_H
#define PROTOBUF_UTILS_H

#include <google/protobuf/message.h>
#include <google/protobuf/any.pb.h>

namespace ProtobufUtils {
   std::string toJson(const google::protobuf::Message &msg, bool addWhitespace = true);
   std::string toJsonReadable(const google::protobuf::Message &msg);
   std::string toJsonCompact(const google::protobuf::Message &msg);
   std::string pbMessageToString(const google::protobuf::Message& msg);
   template<typename T>
   bool pbAnyToMessage(const google::protobuf::Any& any, google::protobuf::Message* msg);
   template<typename T>
   bool pbStringToMessage(const std::string& packetString, google::protobuf::Message* msg);

   bool fromJson(const std::string&, google::protobuf::Message*);
}  // namespace ProtobufUtils

template<typename T>
bool ProtobufUtils::pbAnyToMessage(const google::protobuf::Any& any, google::protobuf::Message* msg)
{
   if (any.Is<T>())
   {
      if (!any.UnpackTo(msg))
      {
         return false;
      }

      return true;
   }

   return false;
}

template<typename T>
bool ProtobufUtils::pbStringToMessage(const std::string& packetString, google::protobuf::Message* msg)
{
   google::protobuf::Any any;
   any.ParseFromString(packetString);

   if (any.Is<T>())
   {
      if (!any.UnpackTo(msg))
      {
         return false;
      }

      return true;
   }

   return false;
}

#endif   //PROTOBUF_UTILS_H
