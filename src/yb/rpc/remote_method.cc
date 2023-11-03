// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/rpc/remote_method.h"

#include "yb/rpc/rpc_header.pb.h"

#include "yb/util/format.h"
#include "yb/util/scope_exit.h"

using std::string;

namespace yb {
namespace rpc {

RemoteMethod::RemoteMethod(std::string service_name,
                           std::string method_name)
    : service_name_(std::move(service_name)), method_name_(std::move(method_name)) {
  RequestHeader pb;
  pb.mutable_remote_method()->set_allocated_service_name(&service_name_);
  pb.mutable_remote_method()->set_allocated_method_name(&method_name_);
  auto se = ScopeExit([&pb] {
    pb.mutable_remote_method()->release_method_name();
    pb.mutable_remote_method()->release_service_name();
  });
  serialized_ = pb.SerializeAsString();
}

void RemoteMethod::ToPB(RemoteMethodPB* pb) const {
  pb->set_service_name(service_name_);
  pb->set_method_name(method_name_);
}

size_t RemoteMethod::DynamicMemoryUsage() const {
  return service_name_.size() + method_name_.size() + serialized_.size();
}

string RemoteMethod::ToString() const {
  return Format("$0.$1", service_name(), method_name());
}

} // namespace rpc
} // namespace yb
