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

#pragma once

#include <stdint.h>
#include <string.h>

#include <atomic>
#include <cstdarg>
#include <mutex>
#include <string>
#include <type_traits>

#include <boost/asio.hpp>
#include <boost/container/small_vector.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/optional/optional_fwd.hpp>
#include <boost/version.hpp>
#include "yb/util/flags.h"

#include "yb/gutil/macros.h"

#include "yb/rpc/service_if.h"

#include "yb/server/server_base.h"
#include "yb/server/ycql_stat_provider.h"
#include "yb/tserver/tserver_fwd.h"

#include "yb/util/status_fwd.h"
#include "yb/util/faststring.h"
#include "yb/util/math_util.h"
#include "yb/util/memory/memory_usage.h"
#include "yb/util/net/net_util.h"
#include "yb/util/net/sockaddr.h"

#include "yb/yql/cql/cqlserver/cql_server_options.h"
#include "yb/yql/cql/ql/util/cql_message.h"

namespace yb {

namespace tserver {
class PgYCQLStatementStatsRequestPB;
class PgYCQLStatementStatsResponsePB;
}

namespace cqlserver {

class CQLServiceImpl;

class CQLServer : public server::RpcAndWebServerBase, public server::YCQLStatementStatsProvider {
 public:
  static const uint16_t kDefaultPort = 9042;
  static const uint16_t kDefaultWebPort = 12000;

  CQLServer(const CQLServerOptions& opts,
            boost::asio::io_service* io,
            tserver::TabletServerIf* tserver);

  Status Start() override;

  void Shutdown() override;

  tserver::TabletServerIf* tserver() const { return tserver_; }

  Status ReloadKeysAndCertificates() override;

  Status YCQLStatementStats(const tserver::PgYCQLStatementStatsRequestPB& req,
      tserver::PgYCQLStatementStatsResponsePB* resp) const override;

  std::shared_ptr<CQLServiceImpl> TEST_cql_service() const { return cql_service_; }

 private:
  CQLServerOptions opts_;
  void CQLNodeListRefresh(const boost::system::error_code &e);
  void RescheduleTimer();
  std::unique_ptr<ql::CQLServerEvent> BuildTopologyChangeEvent(
      const std::string& event_type, const Endpoint& addr);
  Status SetupMessengerBuilder(rpc::MessengerBuilder* builder) override;

  boost::asio::deadline_timer timer_;
  tserver::TabletServerIf* const tserver_;

  std::unique_ptr<rpc::SecureContext> secure_context_;

  std::shared_ptr<CQLServiceImpl> cql_service_;

  DISALLOW_COPY_AND_ASSIGN(CQLServer);
};

} // namespace cqlserver
} // namespace yb
