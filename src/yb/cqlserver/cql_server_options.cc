// Copyright (c) YugaByte, Inc.

#include "yb/cqlserver/cql_server_options.h"

#include "yb/cqlserver/cql_rpc.h"
#include "yb/cqlserver/cql_server.h"

namespace yb {
namespace cqlserver {

CQLServerOptions::CQLServerOptions() {
  rpc_opts.default_port = CQLServer::kDefaultPort;
  connection_context_factory = &std::make_unique<CQLConnectionContext>;
}

} // namespace cqlserver
} // namespace yb
