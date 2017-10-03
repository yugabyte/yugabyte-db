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

#include <glog/logging.h>

#include "kudu/gutil/strings/substitute.h"
#include "kudu/rpc/remote_method.h"
#include "kudu/rpc/rpc_header.pb.h"

namespace kudu {
namespace rpc {

using strings::Substitute;

RemoteMethod::RemoteMethod(std::string service_name,
                           const std::string method_name)
    : service_name_(std::move(service_name)), method_name_(method_name) {}

void RemoteMethod::FromPB(const RemoteMethodPB& pb) {
  DCHECK(pb.IsInitialized()) << "PB is uninitialized: " << pb.InitializationErrorString();
  service_name_ = pb.service_name();
  method_name_ = pb.method_name();
}

void RemoteMethod::ToPB(RemoteMethodPB* pb) const {
  pb->set_service_name(service_name_);
  pb->set_method_name(method_name_);
}

string RemoteMethod::ToString() const {
  return Substitute("$0.$1", service_name_, method_name_);
}

} // namespace rpc
} // namespace kudu
