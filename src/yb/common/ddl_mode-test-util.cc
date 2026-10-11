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

#include "yb/common/ddl_mode-test-util.h"

#include "yb/common/common_flags.h"

#include "yb/util/flags.h"
#include "yb/util/format.h"

DECLARE_bool(ysql_enable_concurrent_ddl);
DECLARE_bool(ysql_yb_ddl_transaction_block_enabled);
DECLARE_bool(ysql_yb_enable_ddl_savepoint_support);
DECLARE_bool(ysql_yb_enable_new_relation_fastpath_write_in_txn_blocks);

namespace yb {

namespace {

/**
  * The flags that make up the new DDL mode which allows:
  * - DDLs in a transaction block and
  * - concurrency with other DMLs and
  * - concurrency with other DDLs
  *
  * They are validated against each other in common_flags.cc, so they can only be moved together.
  */
constexpr const char* kNewDDLModeFlags[] = {
    "ysql_yb_ddl_transaction_block_enabled",
    "enable_object_locking_for_table_locks",
    "ysql_enable_concurrent_ddl"};

// These require transactional DDL, so the legacy mode has to turn them off with it. The new mode
// leaves them at their default rather than forcing them on, so that a build whose default is off
// keeps them off and a test that wants them on can say so itself.
constexpr const char* kFlagsDependingOnTxnalDDL[] = {
    "ysql_yb_enable_ddl_savepoint_support",
    "ysql_yb_enable_new_relation_fastpath_write_in_txn_blocks"};

} // namespace

void ToggleDDLMode(bool use_legacy) {
  const auto enabled = !use_legacy;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_object_locking_for_table_locks) = enabled;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_concurrent_ddl) = enabled;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_ddl_transaction_block_enabled) = enabled;
  if (use_legacy) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_ddl_savepoint_support) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_new_relation_fastpath_write_in_txn_blocks) =
        false;
  }
}

void ToggleDDLMode(std::vector<std::string>& flags, bool use_legacy) {
  const auto* value = use_legacy ? "false" : "true";
  for (const auto* flag : kNewDDLModeFlags) {
    flags.push_back(Format("--$0=$1", flag, value));
  }
  if (use_legacy) {
    for (const auto* flag : kFlagsDependingOnTxnalDDL) {
      flags.push_back(Format("--$0=false", flag));
    }
  }
}

} // namespace yb
