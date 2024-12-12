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

// This file contains the gFlags that are common across yb-master and yb-tserver processes.

#include "yb/util/flags.h"

// User specified identifier for this cluster. On the first master leader setup, this is stored in
// the cluster_config. if not specified, a random UUID is generated.
// Changing the value after setup is not recommended.
DEFINE_NON_RUNTIME_string(cluster_uuid, "", "Cluster UUID to be used by this cluster");
TAG_FLAG(cluster_uuid, hidden);

// NOTE: This flag guards proto changes and it is not safe to enable during an upgrade, or rollback
// once enabled. If you want to change the default to true then you will have to make it a
// kLocalPersisted AutoFlag.
DEFINE_NON_RUNTIME_bool(enable_pg_cron, false,
    "Enables the pg_cron extension. Jobs will be run on a single tserver node. The node should be "
    "assumed to be selected randomly.");

DEFINE_RUNTIME_AUTO_PG_FLAG(
    bool, yb_allow_replication_slot_lsn_types, kLocalPersisted, false, true,
    "Enable LSN types to be specified while creating replication slots.");

// This autoflag was introduced in commit 80def06f8c19ad7cbc52f41b4be48d158157a418, but it had some
// flaws and the feature required a regular gFlag. As of 27.11.2024 there does not exist an infra to
// remove or demote an autoflag, therefore this flag is going to be a dummy flag. A new gFlag
// ycql_ignore_group_by_error in introduced for the same functionality.
DEFINE_RUNTIME_AUTO_bool(ycql_suppress_group_by_error, kLocalVolatile, true, false,
    "This flag is deprecated, please use ycql_ignore_group_by_error");

DEFINE_RUNTIME_bool(yb_enable_advisory_lock, false,
                    "Whether to enable advisory locks.");
