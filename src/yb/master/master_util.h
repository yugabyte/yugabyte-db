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

#ifndef YB_COMMON_MASTER_UTIL_H
#define YB_COMMON_MASTER_UTIL_H

#include <memory>

#include "yb/rpc/messenger.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/master/master.pb.h"
#include "yb/util/net/net_util.h"
#include "yb/util/status.h"

// This file contains utility functions that can be shared between client and master code.

namespace yb {

class MasterUtil {
 public:
  // Given a hostport, return the master server information protobuf.
  // Does not apply to tablet server.
  static CHECKED_STATUS GetMasterEntryForHost(const std::shared_ptr<rpc::Messenger>& messenger,
                                      const HostPort& hostport,
                                      int timeout,
                                      ServerEntryPB* e);

 private:
  MasterUtil();

  DISALLOW_COPY_AND_ASSIGN(MasterUtil);
};

} // namespace yb

#endif
