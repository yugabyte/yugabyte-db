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
#ifndef YB_RPC_REMOTE_METHOD_H_
#define YB_RPC_REMOTE_METHOD_H_

#include <string>

#include <boost/functional/hash.hpp>

#include "yb/rpc/rpc_header.pb.h"
#include "yb/util/memory/memory_usage.h"

namespace yb {
namespace rpc {

class RemoteMethodPB;

// Simple class that acts as a container for a fully qualified remote RPC name.
// This class is also copyable and assignable for convenience reasons.
class RemoteMethod {
 public:
  RemoteMethod() {}
  RemoteMethod(std::string service_name, std::string method_name);
  RemoteMethod(const RemoteMethod& rhs);
  RemoteMethod(RemoteMethod&& rhs);

  RemoteMethod& operator=(const RemoteMethod& rhs);
  RemoteMethod& operator=(const RemoteMethodPB& rhs);
  RemoteMethod& operator=(RemoteMethod&& rhs);

  const std::string& service_name() const { return remote_method_pb_.service_name(); }
  const std::string& method_name() const { return remote_method_pb_.method_name(); }
  const RemoteMethodPB& remote_method_pb() const { return remote_method_pb_; }

  std::string ToString() const;

  size_t DynamicMemoryUsage() const { return DynamicMemoryUsageOf(remote_method_pb_); }

 private:
  RemoteMethodPB remote_method_pb_;
};

} // namespace rpc
} // namespace yb

#endif // YB_RPC_REMOTE_METHOD_H_
