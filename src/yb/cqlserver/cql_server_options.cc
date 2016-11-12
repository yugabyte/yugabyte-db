// Copyright (c) YugaByte, Inc.

#include "yb/cqlserver/cql_server_options.h"

#include "yb/cqlserver/cql_server.h"

DEFINE_int32(cql_service_port, 9042,
             "Port number the CQL service to listen at");

namespace yb {
namespace cqlserver {

CQLServerOptions::CQLServerOptions() {
  connection_type = rpc::ConnectionType::CQL;
  rpc_opts.default_port = FLAGS_cql_service_port;
}

} // namespace cqlserver
} // namespace yb
