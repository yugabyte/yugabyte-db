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
#include <boost/algorithm/string/predicate.hpp>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>

#include "yb/gen_yrpc/model.h"

#include "yb/gutil/strings/util.h"
#include "yb/gutil/strings/split.h"

namespace yb {
namespace gen_yrpc {

namespace {

const std::string kProtoExtension = ".proto";
const std::string kWireFormat = "::google::protobuf::internal::WireFormatLite";

// Extract the last filename component.
std::string GetBaseName(const std::string &path) {
  size_t last_slash = path.find_last_of("/");
  return last_slash != string::npos ? path.substr(last_slash + 1) : path;
}

std::string GenerateOpenNamespace(const std::string& str) {
  std::string out;
  for (const auto& c : strings::Split(str, ".")) {
    out += Format("namespace $0 {\n", c);
  }
  return out;
}

std::string GenerateCloseNamespace(const string &str) {
  std::string out;
  for (const auto& c : strings::Split(str, ".")) {
    out = Format("} // namespace $0\n", c) + out;
  }
  return out;
}

} // namespace

std::string RemoveProtoExtension(const std::string& fname) {
  if (boost::ends_with(fname, kProtoExtension)) {
    return fname.substr(0, fname.length() - kProtoExtension.length());
  } else {
    return fname;
  }
}

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
  auto message_name = UnnestedName(message, false);
  result.emplace_back("message_name", message_name);
  std::string message_pb_name;
  if (message->options().map_entry()) {
    auto key_type = MapFieldType(message->FindFieldByName("key"));
    auto value_type = MapFieldType(message->FindFieldByName("value"));
    message_pb_name = "::google::protobuf::MapPair<" + key_type + ", " + value_type + ">";
  } else {
    message_pb_name = message_name;
  }
  result.emplace_back("message_pb_name", message_pb_name);
  result.emplace_back("message_lw_name", UnnestedName(message, false));
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
  result.emplace_back("request", RelativeClassPath(request_type, method->service()->full_name()));
  result.emplace_back(
      "response", RelativeClassPath(response_type,  method->service()->full_name()));
  result.emplace_back("metric_enum_key", Format("k$0", method->name()));

  return result;
}

Substitutions CreateSubstitutions(const google::protobuf::ServiceDescriptor* service) {
  Substitutions result;
  result.emplace_back("service_name", service->name());
  result.emplace_back("full_service_name", service->full_name());
  result.emplace_back("service_method_count", std::to_string(service->method_count()));
  result.emplace_back("service_method_enum", service->name() + "RpcMethodIndexes");

  // TODO: upgrade to protobuf 2.5.x and attach service comments
  // to the generated service classes using the SourceLocation API.
  return result;
}

} // namespace gen_yrpc
} // namespace yb
