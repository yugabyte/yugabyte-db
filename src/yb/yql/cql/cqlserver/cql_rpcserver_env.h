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

#ifndef YB_YQL_CQL_CQLSERVER_CQL_RPCSERVER_ENV_H
#define YB_YQL_CQL_CQLSERVER_CQL_RPCSERVER_ENV_H

namespace yb {
namespace cqlserver {

class CQLRpcServerEnv {
 public:
  CQLRpcServerEnv(const std::string& rpc_address, const std::string& broadcast_rpc_address)
      : rpc_address_(rpc_address),
        broadcast_rpc_address_(broadcast_rpc_address.empty() ? rpc_address :
                                   broadcast_rpc_address) {
  }

  const std::string& rpc_address() const { return rpc_address_; }

  const std::string& broadcast_address() const { return broadcast_rpc_address_; }

 private:
  // Cassandra equivalent of rpc_address, which is the address the node binds to.
  const std::string rpc_address_;
  // Cassandra equivalent for broadcast_rpc_address, which is the address the node tells other
  // nodes in the cluster.
  const std::string broadcast_rpc_address_;
};

}  // namespace cqlserver
}  // namespace yb

#endif // YB_YQL_CQL_CQLSERVER_CQL_RPCSERVER_ENV_H
