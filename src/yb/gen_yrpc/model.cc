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

#include <boost/algorithm/string/predicate.hpp>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>

#include "yb/gutil/macros.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/split.h"

#include "yb/rpc/service.pb.h"

using google::protobuf::internal::WireFormatLite;

namespace yb {
namespace gen_yrpc {

namespace {

const std::string kProtoExtension = ".proto";

}

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

std::string MakeLightweightName(const std::string& input) {
  auto idx = input.find_last_of('.');
  if (idx != std::string::npos) {
    ++idx;
  } else {
    idx = 0;
  }
  return input.substr(0, idx) + "LW" + input.substr(idx);
}

bool IsLightweightMethod(const google::protobuf::MethodDescriptor* method, rpc::RpcSides side) {
  const auto& options = method->options().GetExtension(rpc::lightweight_method);
  if (side == rpc::RpcSides::BOTH) {
    return options.sides() != rpc::RpcSides::NONE;
  }
  return options.sides() == side || options.sides() == rpc::RpcSides::BOTH;
}

bool IsTrivialMethod(const google::protobuf::MethodDescriptor* method) {
  return method->options().GetExtension(rpc::trivial);
}

bool HasLightweightMethod(const google::protobuf::ServiceDescriptor* service, rpc::RpcSides side) {
  for (int i = 0; i != service->method_count(); ++i) {
    if (IsLightweightMethod(service->method(i), side)) {
      return true;
    }
  }
  return false;
}

bool HasLightweightMethod(const google::protobuf::FileDescriptor* file, rpc::RpcSides side) {
  for (int i = 0; i != file->service_count(); ++i) {
    if (HasLightweightMethod(file->service(i), side)) {
      return true;
    }
  }

  return false;
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

template <class Desc>
std::string DoUnnestedName(
    const Desc* desc, Lightweight lightweight, FullPath full_path, bool map_entry) {
  auto name = lightweight ? MakeLightweightName(desc->name()) : desc->name();
  if (map_entry) {
    name += "_DoNotUse";
  }
  if (desc->containing_type()) {
    return UnnestedName(desc->containing_type(), lightweight, full_path) + "_" + name;
  }
  if (full_path) {
    const auto& full_name = desc->full_name();
    auto idx = full_name.find_last_of('.');
    if (idx != std::string::npos) {
      return full_name.substr(0, idx + 1) + name;
    }
  }
  return name;
}

std::string UnnestedName(
    const google::protobuf::Descriptor* message, Lightweight lightweight, FullPath full_path) {
  return DoUnnestedName(message, lightweight, full_path, message->options().map_entry());
}

std::string UnnestedName(
    const google::protobuf::EnumDescriptor* enum_desc, Lightweight lightweight,
    FullPath full_path) {
  return DoUnnestedName(enum_desc, lightweight, full_path, false);
}

std::string MapFieldType(const google::protobuf::FieldDescriptor* field, Lightweight lightweight) {
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
              ? UnnestedName(
                    field->enum_type()->containing_type(), Lightweight::kFalse, FullPath::kTrue)
                + "." + field->enum_type()->name()
              : field->enum_type()->full_name(),
          field->containing_type()->full_name());
    case WireFormatLite::CppType::CPPTYPE_STRING:
      return lightweight ? "::yb::Slice" : "std::string";
    case WireFormatLite::CppType::CPPTYPE_MESSAGE:
      if (IsPbAny(field->message_type()) && lightweight) {
        return "::yb::rpc::LWAny";
      }
      return RelativeClassPath(UnnestedName(field->message_type(), lightweight, FullPath::kTrue),
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

bool NeedArena(const google::protobuf::Descriptor* message) {
  const auto& options = message->options().GetExtension(rpc::lightweight_message);
  if (options.force_arena()) {
    return true;
  }
  for (int i = 0; i != message->field_count(); ++i) {
    const auto* field = message->field(i);
    if (IsMessage(field) || StoredAsSlice(field)) {
      return true;
    }
  }
  return false;
}

bool IsPointerField(const google::protobuf::FieldDescriptor* field) {
  const auto& lightweight_field_options = field->options().GetExtension(rpc::lightweight_field);
  return lightweight_field_options.pointer();
}

bool StoredAsSlice(const google::protobuf::FieldDescriptor* field) {
  auto type = field->type();
  return type == google::protobuf::FieldDescriptor::TYPE_BYTES ||
         type == google::protobuf::FieldDescriptor::TYPE_STRING;
}

bool IsPbAny(const google::protobuf::Descriptor* message) {
  return message->full_name() == "google.protobuf.Any";
}

bool IsLwAny(const google::protobuf::Descriptor* message) {
  return message->full_name() == "yb.rpc.Any";
}

std::string RemoveProtoExtension(const std::string& fname) {
  if (boost::ends_with(fname, kProtoExtension)) {
    return fname.substr(0, fname.length() - kProtoExtension.length());
  } else {
    return fname;
  }
}

std::vector<std::string> ListDependencies(const google::protobuf::FileDescriptor* file) {
  std::vector<std::string> result;
  for (int i = 0; i != file->dependency_count(); ++i) {
    std::string dependency_path = RemoveProtoExtension(file->dependency(i)->name());
    if (dependency_path == "google/protobuf/any") {
      result.push_back("yb/rpc/any");
      continue;
    }
    if (dependency_path == "yb/rpc/rpc_header" ||
        dependency_path == "yb/rpc/lightweight_message" ||
        dependency_path == "yb/rpc/service" ||
        boost::starts_with(dependency_path, "google/protobuf/")) {
      continue;
    }
    result.push_back(std::move(dependency_path));
  }
  return result;
}

std::optional<std::string> LightweightName(const google::protobuf::EnumDescriptor* enum_desc) {
  const auto& options = enum_desc->options().GetExtension(rpc::lightweight_enum);
  if (options.name().empty()) {
    return std::nullopt;
  }
  return options.name();
}

} // namespace gen_yrpc
} // namespace yb
