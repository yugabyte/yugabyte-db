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
//
// This file contains the CQLServer class that listens for connections from Cassandra clients
// using the CQL native protocol.

#ifndef YB_YQL_CQL_CQLSERVER_CQL_SERVER_H
#define YB_YQL_CQL_CQLSERVER_CQL_SERVER_H

#include <atomic>
#include <string>

#include "yb/yql/cql/cqlserver/cql_server_options.h"
#include "yb/yql/cql/cqlserver/cql_message.h"
#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/macros.h"
#include "yb/server/server_base.h"
#include "yb/tserver/tablet_server.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/status.h"

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

namespace yb {

namespace cqlserver {

class EventResponse;

class CQLServer : public server::RpcAndWebServerBase {
 public:
  static const uint16_t kDefaultPort = 9042;
  static const uint16_t kDefaultWebPort = 12000;

  CQLServer(const CQLServerOptions& opts,
            boost::asio::io_service* io,
            tserver::TabletServer* tserver);

  CHECKED_STATUS Start();

  void Shutdown();

  tserver::TabletServer* tserver() const { return tserver_; }

 private:
  CQLServerOptions opts_;
  void CQLNodeListRefresh(const boost::system::error_code &e);
  void RescheduleTimer();
  boost::asio::deadline_timer timer_;
  tserver::TabletServer* const tserver_;

  std::unique_ptr<CQLServerEvent> BuildTopologyChangeEvent(const std::string& event_type,
                                                           const Endpoint& addr);

  DISALLOW_COPY_AND_ASSIGN(CQLServer);
};

} // namespace cqlserver
} // namespace yb
#endif // YB_YQL_CQL_CQLSERVER_CQL_SERVER_H
