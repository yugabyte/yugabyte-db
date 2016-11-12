// Copyright (c) YugaByte, Inc.
//
// This file contains the CQLServer class that listens for connections from Cassandra clients
// using the CQL native protocol.

#ifndef YB_CQLSERVER_CQL_SERVER_H
#define YB_CQLSERVER_CQL_SERVER_H

#include <string>

#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/macros.h"
#include "yb/cqlserver/cql_server_options.h"
#include "yb/server/server_base.h"
#include "yb/util/status.h"

namespace yb {

namespace cqlserver {

class CQLServer : public server::RpcServerBase {
 public:
  explicit CQLServer(const CQLServerOptions& opts);

  Status Init();

  Status Start();

 private:
  CQLServerOptions opts_;

  DISALLOW_COPY_AND_ASSIGN(CQLServer);
};

} // namespace cqlserver
} // namespace yb
#endif // YB_CQLSERVER_CQL_SERVER_H
