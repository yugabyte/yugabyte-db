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

#pragma once

#include <string>

#include "yb/gutil/macros.h"
#include "yb/yql/redis/redisserver/redis_server_options.h"
#include "yb/server/server_base.h"
#include "yb/tserver/tserver_fwd.h"
#include "yb/util/status_fwd.h"

namespace yb {
namespace redisserver {

class RedisServer : public server::RpcAndWebServerBase {
 public:
  static const uint16_t kDefaultPort = 6379;
  static const uint16_t kDefaultWebPort = 11000;

  explicit RedisServer(const RedisServerOptions& opts, tserver::TabletServerIf* tserver);

  Status Start();

  using server::RpcAndWebServerBase::Shutdown;

  tserver::TabletServerIf* tserver() const { return tserver_; }

  const std::shared_ptr<MemTracker>& mem_tracker() const { return mem_tracker_; }

  const RedisServerOptions& opts() const { return opts_; }

 private:
  RedisServerOptions opts_;
  tserver::TabletServerIf* const tserver_;

  DISALLOW_COPY_AND_ASSIGN(RedisServer);
};

}  // namespace redisserver
}  // namespace yb
