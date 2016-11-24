// Copyright (c) YugaByte, Inc.

#include "yb/cqlserver/cql_server_options.h"

#include "yb/cqlserver/cql_server.h"

namespace yb {
namespace cqlserver {

CQLServerOptions::CQLServerOptions() {
  rpc_opts.default_port = CQLServer::kDefaultPort;
  connection_type = rpc::ConnectionType::CQL;
}

} // namespace cqlserver
} // namespace yb
