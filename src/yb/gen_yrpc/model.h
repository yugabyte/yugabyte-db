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

#pragma once

#include <optional>

#include <google/protobuf/wire_format_lite.h>

#include "yb/gen_yrpc/gen_yrpc_fwd.h"

#include "yb/rpc/lightweight_message.pb.h"

#include "yb/util/strongly_typed_bool.h"

namespace yb {
namespace gen_yrpc {

YB_STRONGLY_TYPED_BOOL(Lightweight);
YB_STRONGLY_TYPED_BOOL(FullPath);

google::protobuf::internal::WireFormatLite::FieldType FieldType(
    const google::protobuf::FieldDescriptor* field);
google::protobuf::internal::WireFormatLite::WireType WireType(
    const google::protobuf::FieldDescriptor* field);
size_t FixedSize(const google::protobuf::FieldDescriptor* field);
std::string MakeLightweightName(const std::string& input);
bool IsLightweightMethod(const google::protobuf::MethodDescriptor* method, rpc::RpcSides side);
bool IsTrivialMethod(const google::protobuf::MethodDescriptor* method);
bool HasLightweightMethod(const google::protobuf::ServiceDescriptor* service, rpc::RpcSides side);
bool HasLightweightMethod(const google::protobuf::FileDescriptor* file, rpc::RpcSides side);
std::string ReplaceNamespaceDelimiters(const std::string& arg_full_name);
std::string RelativeClassPath(const std::string& clazz, const std::string& service);
std::string UnnestedName(
    const google::protobuf::Descriptor* message, Lightweight lightweight, FullPath full_path);
std::string UnnestedName(
    const google::protobuf::EnumDescriptor* enum_desc, Lightweight lightweight, FullPath full_path);
std::string MapFieldType(const google::protobuf::FieldDescriptor* field, Lightweight lightweight);
bool IsMessage(const google::protobuf::FieldDescriptor* field);
bool IsSimple(const google::protobuf::FieldDescriptor* field);
bool NeedArena(const google::protobuf::Descriptor* message);
bool IsPointerField(const google::protobuf::FieldDescriptor* field);
bool StoredAsSlice(const google::protobuf::FieldDescriptor* field);
bool IsPbAny(const google::protobuf::Descriptor* message);
bool IsLwAny(const google::protobuf::Descriptor* message);

std::string RemoveProtoExtension(const std::string& fname);
std::vector<std::string> ListDependencies(const google::protobuf::FileDescriptor* file);

std::optional<std::string> LightweightName(const google::protobuf::EnumDescriptor* enum_desc);

} // namespace gen_yrpc
} // namespace yb
