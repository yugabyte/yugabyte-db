// Copyright (c) YugaByte, Inc.
//
// This file contains the CQLServer class that listens for connections from Cassandra clients
// using the CQL native protocol.

#ifndef YB_CQLSERVER_CQL_SERVER_H
#define YB_CQLSERVER_CQL_SERVER_H

#include <atomic>
#include <string>

#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/macros.h"
#include "yb/cqlserver/cql_server_options.h"
#include "yb/server/server_base.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/status.h"

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

namespace yb {

namespace cqlserver {

class CQLServer : public server::RpcAndWebServerBase {
 public:
  static const uint16_t kDefaultPort = 9042;
  static const uint16_t kDefaultWebPort = 12000;

  // Note that the caller owns the 'io_service' object.
  explicit CQLServer(const CQLServerOptions& opts, boost::asio::io_service* io);

  CHECKED_STATUS Start();

  CHECKED_STATUS Shutdown();

 private:
  CQLServerOptions opts_;
  void CQLNodeListRefresh(const boost::system::error_code &e);
  boost::asio::deadline_timer timer_;

  DISALLOW_COPY_AND_ASSIGN(CQLServer);
};

} // namespace cqlserver
} // namespace yb
#endif // YB_CQLSERVER_CQL_SERVER_H
