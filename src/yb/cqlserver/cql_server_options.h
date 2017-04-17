// Copyright (c) YugaByte, Inc.
//
// This file contains the CQLServerOptions class that defines the CQL server options.

#ifndef YB_CQLSERVER_CQL_SERVER_OPTIONS_H
#define YB_CQLSERVER_CQL_SERVER_OPTIONS_H

#include "yb/server/server_base_options.h"

namespace yb {
namespace cqlserver {

// Options for constructing a CQL server.
class CQLServerOptions : public yb::server::ServerBaseOptions {
 public:
  CQLServerOptions();
  std::string broadcast_rpc_address;

  ~CQLServerOptions() {}
};

} // namespace cqlserver
} // namespace yb
#endif /* YB_CQLSERVER_CQL_SERVER_OPTIONS_H */
