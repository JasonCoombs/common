/*

***********************************************************************************
* Copyright (C) 2019 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "ProtobufUtils.h"

#include <google/protobuf/util/json_util.h>
#include <google/protobuf/any.h>

std::string ProtobufUtils::toJson(const google::protobuf::Message &msg, bool addWhitespace)
{
   std::string result;
   google::protobuf::util::JsonOptions options;
   options.add_whitespace = addWhitespace;
   options.preserve_proto_field_names = true;
   google::protobuf::util::MessageToJsonString(msg, &result, options);
   return result;
}

std::string ProtobufUtils::toJsonReadable(const google::protobuf::Message &msg)
{
   return toJson(msg, true);
}

std::string ProtobufUtils::toJsonCompact(const google::protobuf::Message &msg)
{
   return toJson(msg, false);
}

std::string ProtobufUtils::pbMessageToString(const google::protobuf::Message& msg)
{
   google::protobuf::Any any;
   any.PackFrom(msg);
   return any.SerializeAsString();
}

bool ProtobufUtils::fromJson(const std::string& jsonStr, google::protobuf::Message* msg)
{
   const auto &status = google::protobuf::util::JsonStringToMessage(jsonStr, msg);
   return status.ok();
}
