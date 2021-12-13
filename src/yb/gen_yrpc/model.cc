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

#include "yb/gen_yrpc/model.h"

#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>

#include "yb/gutil/macros.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/split.h"

using google::protobuf::internal::WireFormatLite;

namespace yb {
namespace gen_yrpc {

WireFormatLite::FieldType FieldType(const google::protobuf::FieldDescriptor* field) {
  return static_cast<WireFormatLite::FieldType>(field->type());
}

WireFormatLite::WireType WireType(const google::protobuf::FieldDescriptor* field) {
  return WireFormatLite::WireTypeForFieldType(FieldType(field));
}

size_t FixedSize(const google::protobuf::FieldDescriptor* field) {
  switch (FieldType(field)) {
    case WireFormatLite::TYPE_DOUBLE: FALLTHROUGH_INTENDED;
    case WireFormatLite::TYPE_FIXED64: FALLTHROUGH_INTENDED;
    case WireFormatLite::TYPE_SFIXED64:
      return 8;
    case WireFormatLite::TYPE_FLOAT: FALLTHROUGH_INTENDED;
    case WireFormatLite::TYPE_FIXED32: FALLTHROUGH_INTENDED;
    case WireFormatLite::TYPE_SFIXED32:
      return 4;
    case WireFormatLite::TYPE_BOOL:
      return 1;
    case WireFormatLite::TYPE_INT64: FALLTHROUGH_INTENDED;
    case WireFormatLite::TYPE_UINT64: FALLTHROUGH_INTENDED;
    case WireFormatLite::TYPE_INT32: FALLTHROUGH_INTENDED;
    case WireFormatLite::TYPE_STRING: FALLTHROUGH_INTENDED;
    case WireFormatLite::TYPE_BYTES: FALLTHROUGH_INTENDED;
    case WireFormatLite::TYPE_UINT32: FALLTHROUGH_INTENDED;
    case WireFormatLite::TYPE_ENUM: FALLTHROUGH_INTENDED;
    case WireFormatLite::TYPE_SINT32: FALLTHROUGH_INTENDED;
    case WireFormatLite::TYPE_SINT64: FALLTHROUGH_INTENDED;
    case WireFormatLite::TYPE_MESSAGE: FALLTHROUGH_INTENDED;
    case WireFormatLite::TYPE_GROUP: FALLTHROUGH_INTENDED;
    default:
      return 0;
  }
}

std::string ReplaceNamespaceDelimiters(const std::string& arg_full_name) {
  return JoinStrings(strings::Split(arg_full_name, "."), "::");
}

// Strips the package from method arguments if they are in the same package as
// the service, otherwise leaves them so that we can have fully qualified
// namespaces for method arguments.
std::string RelativeClassPath(const std::string& clazz, const std::string& service) {
  GStringPiece service_package(service);
  auto last_dot = service_package.rfind('.');
  if (last_dot != GStringPiece::npos) {
    service_package = service_package.substr(0, last_dot + 1);

    GStringPiece clazz_path(clazz);
    if (clazz_path.starts_with(service_package)) {
      return ReplaceNamespaceDelimiters(clazz_path.substr(service_package.length()).ToString());
    }
  }

  return "::" + ReplaceNamespaceDelimiters(clazz);
}

std::string UnnestedName(
    const google::protobuf::Descriptor* message, bool full_path) {
  auto name = message->name();
  if (message->options().map_entry()) {
    name += "_DoNotUse";
  }
  if (message->containing_type()) {
    return UnnestedName(message->containing_type(), full_path) + "_" + name;
  }
  if (full_path) {
    const auto& full_name = message->full_name();
    auto idx = full_name.find_last_of('.');
    if (idx != std::string::npos) {
      return full_name.substr(0, idx + 1) + name;
    }
  }
  return name;
}

std::string MapFieldType(const google::protobuf::FieldDescriptor* field) {
  switch (WireFormatLite::FieldTypeToCppType(FieldType(field))) {
    case WireFormatLite::CppType::CPPTYPE_INT32:
      return "int32_t";
    case WireFormatLite::CppType::CPPTYPE_INT64:
      return "int64_t";
    case WireFormatLite::CppType::CPPTYPE_UINT32:
      return "uint32_t";
    case WireFormatLite::CppType::CPPTYPE_UINT64:
      return "uint64_t";
    case WireFormatLite::CppType::CPPTYPE_DOUBLE:
      return "double";
    case WireFormatLite::CppType::CPPTYPE_FLOAT:
      return "float";
    case WireFormatLite::CppType::CPPTYPE_BOOL:
      return "bool";
    case WireFormatLite::CppType::CPPTYPE_ENUM:
      return RelativeClassPath(
          field->enum_type()->containing_type()
              ? UnnestedName(field->enum_type()->containing_type(), true) + "." +
                    field->enum_type()->name()
              : field->enum_type()->full_name(),
          field->containing_type()->full_name());
    case WireFormatLite::CppType::CPPTYPE_STRING:
      return "std::string";
    case WireFormatLite::CppType::CPPTYPE_MESSAGE:
      return RelativeClassPath(UnnestedName(field->message_type(), true),
                               field->containing_type()->full_name());
    default:
      break;
  }
  return "UNSUPPORTED TYPE";
}

bool IsMessage(const google::protobuf::FieldDescriptor* field) {
  return field->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE;
}

bool IsSimple(const google::protobuf::FieldDescriptor* field) {
  return !field->is_repeated() && !IsMessage(field);
}

} // namespace gen_yrpc
} // namespace yb
