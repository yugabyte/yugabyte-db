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
#include "yb/master/master.h"
#include "yb/util/flags.h"

#include "yb/gutil/macros.h"

#include "yb/rpc/service_if.h"

#include "yb/server/server_base.h"

#include "yb/tserver/tserver_fwd.h"

#include "yb/util/status_fwd.h"
#include "yb/util/faststring.h"
#include "yb/util/math_util.h"
#include "yb/util/memory/memory_usage.h"
#include "yb/util/net/net_util.h"
#include "yb/util/net/sockaddr.h"

#include "yb/cdc/cdc_server_options.h"

namespace yb {

namespace cdcserver {

class CDCServiceImpl;
class CDCMasterServer : public server::RpcServerBase {
 public:
  static const uint16_t kDefaultPort = 7200;

  CDCMasterServer(master::Master* master, const CDCServerOptions& opts);

  Status Start() override;
  Status RegisterServices();
  void Shutdown() override;

 private:
  CDCServerOptions opts_;

  // Check if this is required?
  // std::shared_ptr<CDCServiceImpl> cdc_master_service_;

  // define pointer to all the 5 services here itself.
  master::Master* master_;

  DISALLOW_COPY_AND_ASSIGN(CDCMasterServer);
};

}  // namespace cdcserver
}  // namespace yb
