// Copyright (c) YugaByte, Inc.

#ifndef YB_RPC_CQL_RPCSERVER_ENV_H
#define YB_RPC_CQL_RPCSERVER_ENV_H

namespace yb {
namespace rpc {

class CQLRpcServerEnv {
 public:
  CQLRpcServerEnv(const std::string& rpc_address, const std::string& broadcast_rpc_address)
      : rpc_address_(rpc_address),
        broadcast_rpc_address_(broadcast_rpc_address.empty() ? rpc_address :
                                   broadcast_rpc_address) {
  }

  std::string rpc_address() const { return rpc_address_; }

  std::string broadcast_address() const { return broadcast_rpc_address_; }

 private:
  // Cassandra equivalent of rpc_address, which is the address the node binds to.
  const std::string rpc_address_;
  // Cassandra equivalent for broadcast_rpc_address, which is the address the node tells other
  // nodes in the cluster.
  const std::string broadcast_rpc_address_;
};

}  // namespace rpc
}  // namespace yb
#endif // YB_RPC_CQL_RPCSERVER_ENV_H
