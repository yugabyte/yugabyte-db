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

#include "yb/client/table.h"
#include "yb/client/table_info.h"
#include "yb/client/yb_table_name.h"
#include "yb/gutil/bind.h"
#include "yb/gutil/bind_helpers.h"
#include "yb/master/catalog_manager-internal.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/cdc_rpc_tasks.h"
#include "yb/master/master.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_util.h"
#include "yb/master/scoped_leader_shared_lock-internal.h"

#include "yb/util/trace.h"

using std::string;

DEFINE_bool(
    enable_replicate_transaction_status_table, false,
    "Whether to enable xCluster replication of the transaction status table.");

DEFINE_bool(
    check_bootstrap_required, false,
    "Is it necessary to check whether bootstrap is required for Universe Replication.");

namespace yb {
namespace master {
namespace enterprise {
static const string kSystemXClusterReplicationId = "system";

}  // namespace enterprise
}  // namespace master
}  // namespace yb
