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

#include <glog/logging.h>

#include "yb/gutil/strings/substitute.h"
#include "yb/rpc/remote_method.h"
#include "yb/rpc/rpc_header.pb.h"

namespace yb {
namespace rpc {

using strings::Substitute;

RemoteMethod::RemoteMethod(std::string service_name,
                           std::string method_name) {
  remote_method_pb_.set_service_name(std::move(service_name));
  remote_method_pb_.set_method_name(std::move(method_name));
}

RemoteMethod::RemoteMethod(const RemoteMethod& rhs)
    : remote_method_pb_(rhs.remote_method_pb_) {
}

RemoteMethod::RemoteMethod(RemoteMethod&& rhs)
    : remote_method_pb_(std::move(rhs.remote_method_pb_)) {
}

RemoteMethod& RemoteMethod::operator=(const RemoteMethod& rhs) {
  remote_method_pb_ = rhs.remote_method_pb_;
  return *this;
}

RemoteMethod& RemoteMethod::operator=(const RemoteMethodPB& rhs) {
  remote_method_pb_ = rhs;
  return *this;
}

RemoteMethod& RemoteMethod::operator=(RemoteMethod&& rhs) {
  remote_method_pb_ = std::move(rhs.remote_method_pb_);
  return *this;
}

string RemoteMethod::ToString() const {
  return Substitute("$0.$1", service_name(), method_name());
}

} // namespace rpc
} // namespace yb
