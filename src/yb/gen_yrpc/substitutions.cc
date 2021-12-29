// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#include "yb/gen_yrpc/substitutions.h"

#include <boost/algorithm/string/case_conv.hpp>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>

#include "yb/gen_yrpc/model.h"

#include "yb/gutil/strings/util.h"
#include "yb/gutil/strings/split.h"

#include "yb/rpc/service.pb.h"

#include "yb/util/format.h"

namespace yb {
namespace gen_yrpc {

namespace {

const std::string kWireFormat = "::google::protobuf::internal::WireFormatLite";

// Extract the last filename component.
std::string GetBaseName(const std::string &path) {
  size_t last_slash = path.find_last_of("/");
  return last_slash != string::npos ? path.substr(last_slash + 1) : path;
}

std::string GenerateOpenNamespace(const std::string& str) {
  std::string out;
  for (const auto c : strings::Split(str, ".")) {
    out += Format("namespace $0 {\n", c);
  }
  return out;
}

std::string GenerateCloseNamespace(const string &str) {
  std::string out;
  for (const auto c : strings::Split(str, ".")) {
    out = Format("} // namespace $0\n", c) + out;
  }
  return out;
}

} // namespace

FileSubstitutions::FileSubstitutions(const google::protobuf::FileDescriptor* file)
    : file_(file), path_no_extension_(RemoveProtoExtension(file->name())) {
}

Substitutions FileSubstitutions::Create() {
  std::string path = file_->name();
  Substitutions result;

  result.emplace_back("path", path);
  result.emplace_back("path_no_extension", path_no_extension_);

  // If path = /foo/bar/baz_stuff.proto, base_ = baz_stuff
  result.emplace_back("base", GetBaseName(path_no_extension_));

  // If path = /foo/bar/baz_stuff.proto, upper_case_ = BAZ_STUFF
  std::string upper_case = boost::to_upper_copy(path_no_extension_);
  std::replace(upper_case.begin(), upper_case.end(), '/', '_');
  result.emplace_back("upper_case", upper_case);

  result.emplace_back("open_namespace", GenerateOpenNamespace(file_->package()));
  result.emplace_back("close_namespace", GenerateCloseNamespace(file_->package()));

  result.emplace_back("wire_format", kWireFormat);

  return result;
}

Substitutions CreateSubstitutions(const google::protobuf::Descriptor* message) {
  Substitutions result;
  auto message_name = UnnestedName(message, Lightweight::kFalse, false);
  result.emplace_back("message_name", message_name);
  std::string message_pb_name;
  if (IsLwAny(message)) {
    message_pb_name = "::google::protobuf::Any";
  } else if (message->options().map_entry()) {
    auto key_type = MapFieldType(message->FindFieldByName("key"), Lightweight::kFalse);
    auto value_type = MapFieldType(message->FindFieldByName("value"), Lightweight::kFalse);
    message_pb_name = "::google::protobuf::MapPair<" + key_type + ", " + value_type + ">";
  } else {
    message_pb_name = message_name;
  }
  result.emplace_back("message_pb_name", message_pb_name);
  result.emplace_back("message_lw_name", UnnestedName(message, Lightweight::kTrue, false));
  uint32 max_tag = 0;
  for (int i = 0; i != message->field_count(); ++i) {
    auto* field = message->field(i);
    auto wire_type = WireType(field);
    max_tag = std::max(
        max_tag, google::protobuf::internal::WireFormatLite::MakeTag(field->number(), wire_type));
  }

  uint32_t cutoff = 1;
  while (cutoff < max_tag) {
    cutoff = cutoff * 2 + 1;
  }
  result.emplace_back("cutoff", std::to_string(cutoff));
  return result;
}

Substitutions CreateSubstitutions(
    const google::protobuf::MethodDescriptor* method, rpc::RpcSides side) {
  Substitutions result;

  result.emplace_back("rpc_name", method->name());
  result.emplace_back("rpc_full_name", method->full_name());
  result.emplace_back("rpc_full_name_plainchars",
                      StringReplace(method->full_name(), ".", "_", true));

  auto request_type = method->input_type()->full_name();
  auto response_type = method->output_type()->full_name();
  if (IsLightweightMethod(method, side)) {
    request_type = MakeLightweightName(request_type);
    response_type = MakeLightweightName(response_type);
    result.emplace_back("params", "RpcCallLWParams");
  } else {
    result.emplace_back("params", "RpcCallPBParams");
  }
  result.emplace_back("request", RelativeClassPath(request_type, method->service()->full_name()));
  result.emplace_back(
      "response", RelativeClassPath(response_type,  method->service()->full_name()));
  result.emplace_back("metric_enum_key", Format("k$0", method->name()));

  return result;
}

Substitutions CreateSubstitutions(const google::protobuf::FieldDescriptor* field) {
  Substitutions result;
  auto field_name = boost::to_lower_copy(field->name());
  result.emplace_back("field_name", field_name);
  result.emplace_back("field_value", field->is_repeated() ? "entry" : field_name + "()");
  auto camelcase_name = field->camelcase_name();
  camelcase_name[0] = std::toupper(camelcase_name[0]);
  result.emplace_back("field_camelcase_name", camelcase_name);
  std::string field_type = MapFieldType(field, Lightweight::kTrue);
  const char* message_type_format = "::yb::ArenaList<$0>";
  result.emplace_back(
      "field_stored_type",
      field->is_repeated()
          ? Format(IsMessage(field) ? message_type_format : "::yb::MCVector<$0>", field_type)
          : field_type);
  result.emplace_back("field_type", field_type);
  result.emplace_back("nonlw_field_type", MapFieldType(field, Lightweight::kFalse));
  auto field_type_name = "TYPE_" + boost::to_upper_copy(std::string(field->type_name()));
  result.emplace_back("field_type_name", field_type_name);
  result.emplace_back("field_number", std::to_string(field->number()));
  result.emplace_back(
      "field_serialization",
      Format("::yb::rpc::LightweightSerialization<$0::$1, $2>",
             kWireFormat, field_type_name, field_type));
  result.emplace_back(
      "field_serialization_prefix",
      field->is_packed() ? "Packed" : field->is_repeated() ? "Repeated" : "Single");
  return result;
}

Substitutions CreateSubstitutions(const google::protobuf::ServiceDescriptor* service) {
  Substitutions result;
  result.emplace_back("service_name", service->name());
  std::string full_service_name = service->full_name();
  result.emplace_back("original_full_service_name", full_service_name);
  auto custom_service_name = service->options().GetExtension(rpc::custom_service_name);
  if (!custom_service_name.empty()) {
    full_service_name = custom_service_name;
  }
  result.emplace_back("full_service_name", full_service_name);
  result.emplace_back("service_method_count", std::to_string(service->method_count()));
  result.emplace_back("service_method_enum", service->name() + "RpcMethodIndexes");

  // TODO: upgrade to protobuf 2.5.x and attach service comments
  // to the generated service classes using the SourceLocation API.
  return result;
}

} // namespace gen_yrpc
} // namespace yb
