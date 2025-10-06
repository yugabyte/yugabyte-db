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
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include <string>

#include "yb/util/slice.h"

namespace yb {
namespace rpc {

class RemoteMethodPB;

// Simple class that acts as a container for a fully qualified remote RPC name.
// This class is also copyable and assignable for convenience reasons.
class RemoteMethod {
 public:
  RemoteMethod() {}
  RemoteMethod(std::string service_name, std::string method_name);
  RemoteMethod(const RemoteMethod& rhs) = default;
  RemoteMethod(RemoteMethod&& rhs) = default;

  RemoteMethod& operator=(const RemoteMethod& rhs) = default;
  RemoteMethod& operator=(RemoteMethod&& rhs) = default;

  void ToPB(RemoteMethodPB* pb) const;
  const std::string& service_name() const { return service_name_; }
  const std::string& method_name() const { return method_name_; }
  Slice serialized() const { return serialized_; }
  Slice serialized_body() const { return Slice(serialized_).WithoutPrefix(2); }

  std::string ToString() const;

  size_t DynamicMemoryUsage() const;

 private:
  std::string service_name_;
  std::string method_name_;
  std::string serialized_;
};

} // namespace rpc
} // namespace yb
