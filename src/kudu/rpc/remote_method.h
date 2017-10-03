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
#ifndef KUDU_RPC_REMOTE_METHOD_H_
#define KUDU_RPC_REMOTE_METHOD_H_

#include <string>

namespace kudu {
namespace rpc {

class RemoteMethodPB;

// Simple class that acts as a container for a fully qualified remote RPC name
// and converts to/from RemoteMethodPB.
// This class is also copyable and assignable for convenience reasons.
class RemoteMethod {
 public:
  RemoteMethod() {}
  RemoteMethod(std::string service_name, const std::string method_name);
  std::string service_name() const { return service_name_; }
  std::string method_name() const { return method_name_; }

  // Encode/decode to/from 'pb'.
  void FromPB(const RemoteMethodPB& pb);
  void ToPB(RemoteMethodPB* pb) const;

  std::string ToString() const;

 private:
  std::string service_name_;
  std::string method_name_;
};

} // namespace rpc
} // namespace kudu

#endif // KUDU_RPC_REMOTE_METHOD_H_
