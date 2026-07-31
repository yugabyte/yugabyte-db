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

#pragma once

#include <gtest/gtest_prod.h>
#include <stdint.h>
#include <boost/preprocessor.hpp>
#include <boost/preprocessor/arithmetic/dec.hpp>
#include <boost/preprocessor/control/expr_iif.hpp>
#include <boost/preprocessor/control/iif.hpp>
#include <boost/preprocessor/logical/bool.hpp>
#include <boost/preprocessor/punctuation/is_begin_parens.hpp>
#include <boost/preprocessor/repetition/for.hpp>
#include <boost/preprocessor/seq/elem.hpp>
#include <boost/preprocessor/seq/enum.hpp>
#include <boost/preprocessor/seq/fold_left.hpp>
#include <boost/preprocessor/seq/size.hpp>
#include <boost/preprocessor/tuple/elem.hpp>
#include <boost/preprocessor/variadic/elem.hpp>
#include <functional>
#include <shared_mutex>
#include <string>
#include <utility>

#include "yb/server/auto_flags_manager_base.h"
#include "yb/util/flags/auto_flags_util.h"
#include "yb/util/unique_lock.h"
#include "yb/gutil/thread_annotations.h"
#include "yb/server/server_fwd.h"
#include "yb/util/enums.h"
#include "yb/util/result.h"
#include "yb/util/status.h"

namespace yb {
class AutoFlagsConfigPB;
enum class AutoFlagClass;

namespace rpc {
class Messenger;
}  // namespace rpc

namespace master {

class CatalogManager;
class Master;
class DemoteSingleAutoFlagRequestPB;
class DemoteSingleAutoFlagResponsePB;
class GetAutoFlagsConfigRequestPB;
class GetAutoFlagsConfigResponsePB;
class PromoteAutoFlagsRequestPB;
class PromoteAutoFlagsResponsePB;
class PromoteSingleAutoFlagRequestPB;
class PromoteSingleAutoFlagResponsePB;
class RollbackAutoFlagsRequestPB;
class RollbackAutoFlagsResponsePB;
class ValidateAutoFlagsConfigRequestPB;
class ValidateAutoFlagsConfigResponsePB;

YB_DEFINE_ENUM(PromoteAutoFlagsOutcome, (kNoFlagsPromoted)(kNewFlagsPromoted));

// There are four ways in which a new config is loaded on the yb-masters.
//
// LoadFromFile and LoadFromMasterLeader as described in AutoFlagsManagerBase.
//
// LoadNewConfig - Stores a new config on cluster create or on the first upgrade from a version
// without AutoFlags to a version with AutoFlags.
//
// ProcessAutoFlagsConfigOperation -Processes the ChangeAutoFlagsConfigOperation WAL operation on
// masters. Stores the new config to local disk immediately, and asynchronously applies the config
// at the provided config_apply_time.
//
// All config changes happen via StoreConfig on the master leader.
// The master leader picks the config_apply_time and commits the config via a
// ChangeAutoFlagsConfigOperation. Heartbeat responses are blocked for the duration of this
// function to guarantee correctness. The apply of the WAL operation will trigger
// ProcessAutoFlagsConfigOperation on all master (including leader).
class MasterAutoFlagsManager : public AutoFlagsManagerBase {
 public:
  explicit MasterAutoFlagsManager(Master& master);

  virtual ~MasterAutoFlagsManager() {}

  Status Init(
      rpc::Messenger* messenger, std::function<bool()> has_sys_catalog_func, bool is_shell_mode);

  Status LoadFromMasterLeader(const server::MasterAddresses& master_addresses);

  Status ProcessAutoFlagsConfigOperation(const AutoFlagsConfigPB new_config) override;

  // RPC handlers.
  Status GetAutoFlagsConfig(
      const GetAutoFlagsConfigRequestPB* req, GetAutoFlagsConfigResponsePB* resp);
  Status PromoteAutoFlags(const PromoteAutoFlagsRequestPB* req, PromoteAutoFlagsResponsePB* resp);
  Status RollbackAutoFlags(
      const RollbackAutoFlagsRequestPB* req, RollbackAutoFlagsResponsePB* resp);
  Status PromoteSingleAutoFlag(
      const PromoteSingleAutoFlagRequestPB* req, PromoteSingleAutoFlagResponsePB* resp);
  Status DemoteSingleAutoFlag(
      const DemoteSingleAutoFlagRequestPB* req, DemoteSingleAutoFlagResponsePB* resp);
  Status ValidateAutoFlagsConfig(
      const ValidateAutoFlagsConfigRequestPB* req, ValidateAutoFlagsConfigResponsePB* resp);

 private:
  FRIEND_TEST(AutoFlagsMiniClusterTest, CheckMissingFlag);

  Status LoadNewConfig(const AutoFlagsConfigPB new_config) EXCLUDES(mutex_);

  // Create and persist a empty AutoFlags config with version set to 1.
  // Intended to be used during the first process startup after the upgrade of clusters created on
  // versions without AutoFlags.
  Status CreateEmptyConfig();

  // Create and persist a new AutoFlags config where all AutoFlags of class within
  // FLAGS_limit_auto_flag_promote_for_new_universe are promoted and Apply it.
  // Intended to be used in new cluster created with AutoFlags.
  Status CreateConfigForNewCluster();

  // Set the config apply time and persist it in the sys_catalog.
  Status StoreConfig(AutoFlagsConfigPB& new_config);

  Status PersistConfigInSysCatalog(AutoFlagsConfigPB& new_config);

  // Promote eligible AutoFlags up to max_flag_class. If no new flags were eligible, Status
  // AlreadyPresent is returned. When force is set, the config version is bumped up even if no new
  // flags are eligible. Returns the new config version and whether any non-runtime flags were
  // promoted.
  Result<std::pair<uint32_t, PromoteAutoFlagsOutcome>> PromoteAutoFlags(
      const AutoFlagClass max_flag_class, const bool force_version_change);

  Result<std::pair<uint32_t, PromoteAutoFlagsOutcome>> PromoteSingleAutoFlag(
      const ProcessName& process_name, const std::string& flag_name);

  // Rollback AutoFlags to the specified version. Only Volatile AutoFlags are eligible for rollback.
  // Returns weather any flags were rolled back and the new config version.
  Result<std::pair<uint32_t, bool>> RollbackAutoFlags(uint32_t rollback_version);

  // Demote a single AutoFlag. Returns weather the flag was demoted and the new config version.
  // Note: This is extremely dangerous and should only be used under the guidance of YugabyteDB
  // engineering team.
  Result<std::pair<uint32_t, bool>> DemoteSingleAutoFlag(
      const ProcessName& process_name, const std::string& flag_name);

  Master& master_;
  CatalogManager* catalog_manager_;
  UniqueLock<std::shared_mutex> update_lock_;
};

}  // namespace master
}  // namespace yb
