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

#include "yb/client/auto_flags_manager.h"
#include "yb/client/client.h"

#include "yb/fs/fs_manager.h"

#include "yb/gutil/strings/join.h"

#include "yb/master/master_cluster.pb.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/secure_stream.h"

#include "yb/server/secure.h"

#include "yb/util/flags/auto_flags.h"
#include "yb/util/net/net_util.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/scope_exit.h"
#include "yb/util/source_location.h"
#include "yb/util/thread_restrictions.h"
#include "yb/util/version_info.h"

DEFINE_NON_RUNTIME_bool(disable_auto_flags_management, false,
    "Disables AutoFlags management. A safety switch to turn off automatic promotion of AutoFlags. "
    "More information about AutoFlags can be found in "
    "https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/auto_flags.md. Use at "
    "your own risk!");
TAG_FLAG(disable_auto_flags_management, advanced);
TAG_FLAG(disable_auto_flags_management, unsafe);

DEFINE_RUNTIME_AUTO_bool(TEST_auto_flags_initialized, kLocalPersisted, false, true,
    "AutoFlag that indicates initialization of AutoFlags. Not meant to be overridden.");
TAG_FLAG(TEST_auto_flags_initialized, hidden);

DEFINE_RUNTIME_AUTO_bool(TEST_auto_flags_new_install, kNewInstallsOnly, false, true,
    "AutoFlag that indicates initialization of AutoFlags for new installs only.");
TAG_FLAG(TEST_auto_flags_new_install, hidden);

DEFINE_NON_RUNTIME_int32(auto_flags_load_from_master_backoff_increment_ms, 100,
    "Number of milliseconds added to the delay between reties of fetching AutoFlags config from "
    "master leader. This delay is applied after the RPC reties have been exhausted.");
TAG_FLAG(auto_flags_load_from_master_backoff_increment_ms, stable);
TAG_FLAG(auto_flags_load_from_master_backoff_increment_ms, advanced);

DEFINE_NON_RUNTIME_int32(auto_flags_load_from_master_max_backoff_sec, 3,
    "Maximum number of seconds to delay between reties of fetching AutoFlags config from master "
    "leader. This delay is applied after the RPC reties have been exhausted.");
TAG_FLAG(auto_flags_load_from_master_max_backoff_sec, stable);
TAG_FLAG(auto_flags_load_from_master_max_backoff_sec, advanced);

DEFINE_RUNTIME_uint32(auto_flags_apply_delay_ms, 10000,
    "Number of milliseconds after which a new AutoFlag config is applied. yb-tservers that have "
    "not heartbeated to yb-master within this duration cannot replicate data to XCluster targets. "
    "The value must be at least twice the heartbeat_interval_ms.");
TAG_FLAG(auto_flags_apply_delay_ms, stable);
TAG_FLAG(auto_flags_apply_delay_ms, advanced);

DECLARE_bool(TEST_running_test);
DECLARE_int32(yb_client_admin_operation_timeout_sec);
DECLARE_uint64(max_clock_skew_usec);

namespace yb {

namespace {

std::unordered_set<std::string> GetPerProcessFlags(
    const ProcessName& process_name, const AutoFlagsConfigPB& config) {
  std::unordered_set<std::string> flags;
  for (auto& per_process_flags : config.promoted_flags()) {
    if (per_process_flags.process_name() == process_name) {
      for (auto& flag : per_process_flags.flags()) {
        flags.insert(flag);
      }
      break;
    }
  }

  return flags;
}

}  // namespace

AutoFlagsManager::AutoFlagsManager(
    const std::string& process_name, const scoped_refptr<ClockBase>& clock, FsManager* fs_manager)
    : process_name_(process_name),
      clock_(clock),
      fs_manager_(fs_manager),
      update_lock_(mutex_, std::defer_lock) {
  // google::ProgramInvocationShortName() cannot be used for process_name as it will return the test
  // name in MiniCluster tests.
  current_config_.set_config_version(kInvalidAutoFlagsConfigVersion);
}

AutoFlagsManager::~AutoFlagsManager() {
  if (messenger_) {
    messenger_->Shutdown();
  }
}

Status AutoFlagsManager::Init(const std::string& local_hosts) {
  rpc::MessengerBuilder messenger_builder("auto_flags_client");
  secure_context_ = VERIFY_RESULT(
      server::SetupInternalSecureContext(local_hosts, *fs_manager_, &messenger_builder));

  messenger_ = VERIFY_RESULT(messenger_builder.Build());

  if (PREDICT_FALSE(FLAGS_TEST_running_test)) {
    std::vector<HostPort> host_ports;
    RETURN_NOT_OK(HostPort::ParseStrings(local_hosts, 0 /* default_port */, &host_ports));
    messenger_->TEST_SetOutboundIpBase(VERIFY_RESULT(HostToAddress(host_ports[0].host())));
  }

  return Status::OK();
}

Result<bool> AutoFlagsManager::LoadFromFile() {
  if (FLAGS_disable_auto_flags_management) {
    LOG(WARNING) << "AutoFlags management is disabled.";
    return true;
  }

  std::lock_guard update_lock(mutex_);

  AutoFlagsConfigPB pb_config;
  auto status = fs_manager_->ReadAutoFlagsConfig(&pb_config);
  if (!status.ok()) {
    if (status.IsNotFound()) {
      return false;
    }

    return status;
  }

  auto valid = VERIFY_RESULT(ValidateAndSetConfig(std::move(pb_config)));
  RSTATUS_DCHECK(valid, IllegalState, "AutoFlags config loaded from disk failed to get set");

  RETURN_NOT_OK(ApplyConfig(ApplyNonRuntimeAutoFlags::kTrue));

  return true;
}

Result<std::optional<AutoFlagsConfigPB>> AutoFlagsManager::GetAutoFlagConfigFromMaster(
    const std::string& master_addresses) {
  auto client = VERIFY_RESULT(yb::client::YBClientBuilder()
                                  .add_master_server_addr(master_addresses)
                                  .default_admin_operation_timeout(MonoDelta::FromSeconds(
                                      FLAGS_yb_client_admin_operation_timeout_sec))
                                  .Build(messenger_.get()));

  return client->GetAutoFlagConfig();
}

Status AutoFlagsManager::LoadFromMaster(
    const std::string& local_hosts, const server::MasterAddresses& master_addresses) {
  if (FLAGS_disable_auto_flags_management) {
    LOG(WARNING) << "AutoFlags management is disabled.";
    return Status::OK();
  }

  std::vector<std::string> addresses;
  for (const auto& address : master_addresses) {
    for (const auto& host_port : address) {
      addresses.push_back(host_port.ToString());
    }
  }

  const auto master_addresses_str = JoinStrings(addresses, ",");

  SCHECK(
      !master_addresses_str.empty(), InvalidArgument,
      "No master address found to initialize AutoFlags.");

  // Get lock early to make sure we dont send multiple RPCs to master leader.
  std::lock_guard l(mutex_);
  LOG(INFO) << "Loading AutoFlags from master leader. Master addresses: [" << master_addresses_str
            << "]";

  AutoFlagsConfigPB new_config;

  const auto delay_increment =
      MonoDelta::FromMilliseconds(FLAGS_auto_flags_load_from_master_backoff_increment_ms);
  const auto max_delay_time =
      MonoDelta::FromSeconds(FLAGS_auto_flags_load_from_master_max_backoff_sec);
  auto delay_time = delay_increment;

  uint32_t attempts = 1;
  auto start_time = clock_->Now();
  while (true) {
    auto res = GetAutoFlagConfigFromMaster(master_addresses_str);
    if (res.ok()) {
      if (res->has_value()) {
        new_config = std::move(res->value());
      } else {
        // Master is running on older version which does not support AutoFlags.
        // Use a empty config. Once master leader is upgraded to a supported version, it will send
        // the new config through the WAL to other masters and through heartbeats to tservers.
        LOG(INFO) << "AutoFlags not yet initialized on master. Defaulting to empty config.";
        new_config.set_config_version(kInvalidAutoFlagsConfigVersion);
      }

      break;
    }

    LOG(WARNING) << "Loading AutoFlags from master Leader failed: '" << res.status()
                 << "'. Attempts: " << attempts << ", Total Time: "
                 << clock_->Now().PhysicalDiff(start_time) / MonoTime::kMicrosecondsPerMillisecond
                 << "ms. Retrying...";

    // Delay before retrying so that we don't accidentally DDoS the mater.
    // Time increases linearly by delay_increment up to max_delay.
    SleepFor(delay_time);
    delay_time = std::min(max_delay_time, delay_time + delay_increment);
    attempts++;
  }

  // Synchronously load the config.
  return LoadFromConfigUnlocked(
      std::move(new_config), ApplyNonRuntimeAutoFlags::kTrue, /* apply_sync */ true);
}

Status AutoFlagsManager::LoadNewConfig(const AutoFlagsConfigPB new_config) {
  std::lock_guard l(mutex_);
  return LoadFromConfigUnlocked(new_config, ApplyNonRuntimeAutoFlags::kTrue);
}

Result<MonoDelta> AutoFlagsManager::GetTimeLeftToApplyConfig() const {
  static const MonoDelta uninitialized_delta;

  if (!current_config_.has_config_apply_time()) {
    return uninitialized_delta;
  }

  HybridTime apply_ht;
  RETURN_NOT_OK(apply_ht.FromUint64(current_config_.config_apply_time()));
  const auto now = clock_->Now();
  if (now < apply_ht) {
    return MonoDelta::FromMicroseconds(apply_ht.PhysicalDiff(now));
  }

  return uninitialized_delta;
}

Status AutoFlagsManager::LoadFromConfigUnlocked(
    const AutoFlagsConfigPB new_config, ApplyNonRuntimeAutoFlags apply_non_runtime,
    bool apply_sync) {
  if (!VERIFY_RESULT(ValidateAndSetConfig(std::move(new_config)))) {
    // No-op if the config is the same or lower version.
    return Status::OK();
  }

  RETURN_NOT_OK(WriteConfigToDisk());

  if (!apply_sync) {
    const auto delay = VERIFY_RESULT(GetTimeLeftToApplyConfig());
    if (delay) {
      LOG(INFO) << "New AutoFlags config will be applied in " << delay;
      RETURN_NOT_OK(messenger_->ScheduleOnReactor(
          std::bind(
              &AutoFlagsManager::AsyncApplyConfig, this, current_config_.config_version(),
              apply_non_runtime),
          delay, SOURCE_LOCATION()));
      return Status::OK();
    }
  }

  return ApplyConfig(apply_non_runtime);
}

uint32_t AutoFlagsManager::GetConfigVersion() const {
  SharedLock lock(mutex_);
  return current_config_.config_version();
}

Result<uint32_t> AutoFlagsManager::ValidateAndGetConfigVersion() const {
  const auto last_config_sync_time = last_config_sync_time_.load(std::memory_order_acquire);
  SCHECK(last_config_sync_time, IllegalState, "AutoFlags config is stale. No config sync time set");
  const auto apply_delay = GetApplyDelay();
  const auto max_allowed_time =
      last_config_sync_time.AddDelta(apply_delay)
          .AddDelta(MonoDelta::FromMicroseconds(-1 * FLAGS_max_clock_skew_usec));

  const auto now = clock_->Now();
  SCHECK_LT(
      now, max_allowed_time, IllegalState,
      Format(
          "AutoFlags config is stale. Last sync time: $0, Max allowed staleness: $1",
          last_config_sync_time, apply_delay));

  return GetConfigVersion();
}

AutoFlagsConfigPB AutoFlagsManager::GetConfig() const {
  SharedLock lock(mutex_);
  return current_config_;
}

MonoDelta AutoFlagsManager::GetApplyDelay() const {
  return MonoDelta::FromMilliseconds(FLAGS_auto_flags_apply_delay_ms);
}

Result<bool> AutoFlagsManager::ValidateAndSetConfig(const AutoFlagsConfigPB&& new_config) {
  // First new config can be empty, and should still be written to disk.
  // Else no-op if it is the same or lower version.
  const auto& current_version = current_config_.config_version();
  const auto& new_version = new_config.config_version();
  if (current_version != kInvalidAutoFlagsConfigVersion && new_version <= current_version) {
    LOG(INFO) << "AutoFlags config update ignored as we are already on the same"
                 " or higher version. Current version: "
              << current_config_.config_version()
              << ", New version: " << new_config.config_version();
    return false;
  }

  const auto required_promoted_flags = GetPerProcessFlags(process_name_, new_config);
  for (const auto& flag_name : required_promoted_flags) {
    // This will fail if the node is running a old version of the code that does not support the
    // flag.
    SCHECK(
        GetAutoFlagDescription(flag_name) != nullptr, NotFound,
        "AutoFlag '$0' is not found. Upgrade the process to a version that contains this AutoFlag. "
        "Current version: $1",
        flag_name, VersionInfo::GetShortVersionString());
  }

  current_config_ = std::move(new_config);

  return true;
}

Status AutoFlagsManager::WriteConfigToDisk() {
  LOG(INFO) << "Storing new AutoFlags config: " << current_config_.ShortDebugString();
  RETURN_NOT_OK_PREPEND(
      fs_manager_->WriteAutoFlagsConfig(&current_config_), "Failed to store AutoFlag config");

  return Status::OK();
}

void AutoFlagsManager::AsyncApplyConfig(
    uint32 apply_version, ApplyNonRuntimeAutoFlags apply_non_runtime) {
  SharedLock lock(mutex_);
  if (current_config_.config_version() != apply_version) {
    LOG(INFO) << "Skipping AutoFlags apply as the config version has changed. Expected: "
              << apply_version << ", Actual: " << current_config_.config_version();
    return;
  }

  CHECK_OK_PREPEND(ApplyConfig(apply_non_runtime), "Failed to Apply AutoFlags");
}

// This is a blocking function that can block process startup. We relax ThreadRestrictions since it
// gets invoked from a reactor thread.
Status AutoFlagsManager::ApplyConfig(ApplyNonRuntimeAutoFlags apply_non_runtime) const {
  const auto delay = VERIFY_RESULT(GetTimeLeftToApplyConfig());
  if (delay) {
    LOG(INFO) << "Sleeping for " << delay << "us before applying AutoFlags.";
    ThreadRestrictions::ScopedAllowWait scoped_allow_wait;
    SleepFor(delay);
  }

  const auto required_promoted_flags = GetPerProcessFlags(process_name_, current_config_);
  std::vector<std::string> flags_promoted;
  std::vector<std::string> flags_demoted;
  std::vector<std::string> non_runtime_flags_skipped;

  std::unordered_set<std::string> server_auto_flags;
  {
    ThreadRestrictions::ScopedAllowIO scoped_allow_io;
    server_auto_flags = VERIFY_RESULT(GetAvailableAutoFlagsForServer());
  }

  for (auto& flag_name : server_auto_flags) {
    auto* flag_desc = CHECK_NOTNULL(GetAutoFlagDescription(flag_name));
    gflags::CommandLineFlagInfo flag_info;
    CHECK(GetCommandLineFlagInfo(flag_name.c_str(), &flag_info));
    bool is_flag_promoted = IsFlagPromoted(flag_info, *flag_desc);

    if (required_promoted_flags.contains(flag_desc->name)) {
      if (!is_flag_promoted) {
        if (apply_non_runtime || flag_desc->is_runtime) {
          PromoteAutoFlag(*flag_desc);
          flags_promoted.push_back(flag_desc->name);
        } else {
          non_runtime_flags_skipped.push_back(flag_desc->name);
        }
      }
      // else - Flag is already promoted. No-op.
    } else if (is_flag_promoted) {
      DemoteAutoFlag(*flag_desc);
      flags_demoted.push_back(flag_desc->name);
    }
    // else - Flag is not promoted and not required to be promoted. No-op.
  }

  if (!flags_promoted.empty()) {
    LOG(INFO) << "AutoFlags promoted: " << JoinStrings(flags_promoted, ",");
  }
  if (!flags_demoted.empty()) {
    LOG(INFO) << "AutoFlags demoted: " << JoinStrings(flags_demoted, ",");
  }
  if (!non_runtime_flags_skipped.empty()) {
    LOG(WARNING) << "Non-runtime AutoFlags skipped apply: "
                 << JoinStrings(non_runtime_flags_skipped, ",")
                 << ". Restart the process to apply these flags.";
  }

  return Status::OK();
}

Result<std::unordered_set<std::string>> AutoFlagsManager::GetAvailableAutoFlagsForServer() const {
  auto all_auto_flags = VERIFY_RESULT(AutoFlagsUtil::GetAvailableAutoFlags());
  std::unordered_set<std::string> process_auto_flags;
  for (const auto& flag : all_auto_flags[process_name_]) {
    process_auto_flags.insert(flag.name);
  }
  return process_auto_flags;
}

// No thread safety analysis, as it cannot detect that the mutex is locked by UniqueLock.
Status AutoFlagsManager::StoreUpdatedConfig(
    AutoFlagsConfigPB& new_config,
    std::function<Status(const AutoFlagsConfigPB&)> persist_config_func) NO_THREAD_SAFETY_ANALYSIS {
  // The config has to get quorum committed in the sys_catalog before it can be stored in
  // current_config_ even on the master leader. So, there will be delay between when the
  // config_apply_time is computed and it being stored.
  // During this window we should not respond to heartbeats since it will renew the leases in the
  // tserver for auto_flags_apply_delay_ms, which can cause it to be higher than the
  // config_apply_time we picked. We hold onto the mutex_ so that we do not respond to heartbeats.
  //
  // Raft and master leader election will guarantee that this is safe from crashes:
  // If we crash between writing the WAL op and the it getting applied, then the new leader will
  // apply it before responding to heartbeats. This is because new leader will have to commit the
  // NO_OP record and apply all pending operations before it is marked ready. If the op was never
  // replicated to the new leader then the operation will be lost, and the user will have to try
  // again.
  RSTATUS_DCHECK(
      !update_lock_.owns_lock(), IllegalState, "AutoFlags config update already in progress");
  update_lock_.lock();
  auto se = ScopeExit([this]() NO_THREAD_SAFETY_ANALYSIS { update_lock_.unlock(); });

  // This is an update of an existing config. The initial config must be applied immediately.
  DCHECK_GE(new_config.config_version(), kMinAutoFlagsConfigVersion);
  // Every config change must update the version by 1.
  RSTATUS_DCHECK_EQ(
      new_config.config_version(), current_config_.config_version() + 1, IllegalState,
      "Attempting to store a stale config");

  const auto now = clock_->Now();
  const auto config_apply_ht = now.AddDelta(GetApplyDelay());
  new_config.set_config_apply_time(config_apply_ht.ToUint64());

  return persist_config_func(new_config);
}

// No thread safety analysis, as it cannot detect that the mutex is locked by UniqueLock.
Status AutoFlagsManager::ProcessAutoFlagsConfigOperation(const AutoFlagsConfigPB new_config)
    NO_THREAD_SAFETY_ANALYSIS {
  bool unlock_needed = false;
  auto se = ScopeExit([&update_lock = update_lock_, &unlock_needed]() NO_THREAD_SAFETY_ANALYSIS {
    if (unlock_needed) {
      update_lock.unlock();
    }
  });

  // This function will be invoked when the ChangeAutoFlagsConfigOperation is applied. The
  // StoreUpdatedConfig may be holding the lock already and waiting for us to complete in which case
  // we do not have to reacquire the lock. If we crashed during StoreUpdatedConfig, then the
  // operation can get applied at tablet bootstrap or a later time, and in both cases we need to get
  // the lock.
  if (!update_lock_.owns_lock()) {
    update_lock_.lock();
    unlock_needed = true;
  }

  return LoadFromConfigUnlocked(std::move(new_config), ApplyNonRuntimeAutoFlags::kFalse);
}

void AutoFlagsManager::HandleMasterHeartbeatResponse(
    HybridTime heartbeat_sent_time, std::optional<AutoFlagsConfigPB> new_config) {
  if (new_config) {
    std::lock_guard l(mutex_);
    // We cannot fail to load a new config that was provided by the master.
    CHECK_OK(LoadFromConfigUnlocked(std::move(*new_config), ApplyNonRuntimeAutoFlags::kFalse));
  }
  last_config_sync_time_.store(heartbeat_sent_time, std::memory_order_release);
}
}  // namespace yb
