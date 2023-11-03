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

#include <map>
#include <string>

#include "yb/gen_yrpc/gen_yrpc_fwd.h"

#include "yb/rpc/lightweight_message.pb.h"

#include "yb/util/status.h"

namespace yb {
namespace gen_yrpc {

class FileSubstitutions {
 public:
  explicit FileSubstitutions(const google::protobuf::FileDescriptor* file);

  Substitutions Create();

  std::string service() const {
    return path_no_extension_ + ".service";
  }

  std::string proxy() const {
    return path_no_extension_ + ".proxy";
  }

  std::string messages() const {
    return path_no_extension_ + ".messages";
  }

  std::string forward() const {
    return path_no_extension_ + ".fwd";
  }

 private:
  const google::protobuf::FileDescriptor* file_;
  std::string path_no_extension_;
};

Substitutions CreateSubstitutions(const google::protobuf::Descriptor* message);
Substitutions CreateSubstitutions(const google::protobuf::EnumDescriptor* enum_desc);
Substitutions CreateSubstitutions(
    const google::protobuf::MethodDescriptor* method, rpc::RpcSides side);
Substitutions CreateSubstitutions(const google::protobuf::FieldDescriptor* field);
Substitutions CreateSubstitutions(const google::protobuf::ServiceDescriptor* service);

} // namespace gen_yrpc
} // namespace yb
