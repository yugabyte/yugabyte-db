// Copyright (c) YugabyteDB, Inc.
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

#include "yb/common/ysql_operation_lease.h"

#include "yb/common/common_flags.h"

#include "yb/util/atomic.h"
#include "yb/util/flags/auto_flags.h"

DEFINE_RUNTIME_bool(enable_ysql_operation_lease, true, "Enables the ysql client operation lease. "
                    "The client operation lease must " "be held by a tserver to host pg sessions. "
                    "It is refreshed by the master leader.");

namespace yb {

bool IsYsqlLeaseEnabled() {
  return FLAGS_enable_object_locking_for_table_locks ||
         FLAGS_enable_ysql_operation_lease;
}

}  // namespace yb
