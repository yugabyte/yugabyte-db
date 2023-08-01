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

using std::string;

DEFINE_UNKNOWN_bool(disable_auto_flags_management, false,
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

DEFINE_UNKNOWN_int32(auto_flags_load_from_master_backoff_increment_ms, 100,
    "Number of milliseconds added to the delay between reties of fetching AutoFlags config from "
    "master leader. This delay is applied after the RPC reties have been exhausted.");
TAG_FLAG(auto_flags_load_from_master_backoff_increment_ms, stable);
TAG_FLAG(auto_flags_load_from_master_backoff_increment_ms, advanced);

DEFINE_UNKNOWN_int32(auto_flags_load_from_master_max_backoff_sec, 3,
    "Maximum number of seconds to delay between reties of fetching AutoFlags config from master "
    "leader. This delay is applied after the RPC reties have been exhausted.");
TAG_FLAG(auto_flags_load_from_master_max_backoff_sec, stable);
TAG_FLAG(auto_flags_load_from_master_max_backoff_sec, advanced);

DECLARE_bool(TEST_running_test);
DECLARE_int32(yb_client_admin_operation_timeout_sec);

namespace yb {

namespace {

class AutoFlagClient {
 public:
  Status Init(
      const string& local_hosts, const string& master_addresses, const FsManager& fs_manager) {
    rpc::MessengerBuilder messenger_builder("auto_flags_client");
    secure_context_ = VERIFY_RESULT(
        server::SetupInternalSecureContext(local_hosts, fs_manager, &messenger_builder));

    messenger_ = VERIFY_RESULT(messenger_builder.Build());

    if (PREDICT_FALSE(FLAGS_TEST_running_test)) {
      std::vector<HostPort> host_ports;
      RETURN_NOT_OK(HostPort::ParseStrings(local_hosts, 0 /* default_port */, &host_ports));
      messenger_->TEST_SetOutboundIpBase(VERIFY_RESULT(HostToAddress(host_ports[0].host())));
    }

    client_ = VERIFY_RESULT(yb::client::YBClientBuilder()
                                .add_master_server_addr(master_addresses)
                                .default_admin_operation_timeout(MonoDelta::FromSeconds(
                                    FLAGS_yb_client_admin_operation_timeout_sec))
                                .Build(messenger_.get()));

    return Status::OK();
  }

  client::YBClient* operator->() { return client_.get(); }

  ~AutoFlagClient() {
    if (messenger_) {
      messenger_->Shutdown();
    }
  }

 private:
  std::unique_ptr<rpc::SecureContext> secure_context_;
  std::unique_ptr<rpc::Messenger> messenger_;
  std::shared_ptr<client::YBClient> client_;
};

// Get the AutoFlagConfig from master. Returns std::nullopt if master is runnning on an older
// version that does not support AutoFlags.
Result<std::optional<AutoFlagsConfigPB>> GetAutoFlagConfigFromMaster(
    const string& local_hosts, const string& master_addresses, const FsManager& fs_manager) {
  AutoFlagClient af_client;
  RETURN_NOT_OK(af_client.Init(local_hosts, master_addresses, fs_manager));

  return af_client->GetAutoFlagConfig();
}

google::protobuf::RepeatedPtrField<string>* GetPerProcessFlags(
    const string& process_name, AutoFlagsConfigPB* config) {
  for (auto& per_process_flags : *config->mutable_promoted_flags()) {
    if (per_process_flags.process_name() == process_name) {
      return per_process_flags.mutable_flags();
    }
  }

  return nullptr;
}

}  // namespace

AutoFlagsManager::AutoFlagsManager(const string& process_name, FsManager* fs_manager)
    : process_name_(process_name), fs_manager_(fs_manager) {
  // google::ProgramInvocationShortName() cannot be used for process_name as it will return the test
  // name in MiniCluster tests.
  current_config_.set_config_version(0);
}

Result<bool> AutoFlagsManager::LoadFromFile() {
  if (FLAGS_disable_auto_flags_management) {
    LOG(WARNING) << "AutoFlags management is disabled.";
    return true;
  }

  std::lock_guard update_lock(update_mutex_);

  AutoFlagsConfigPB pb_config;
  auto status = fs_manager_->ReadAutoFlagsConfig(&pb_config);
  if (!status.ok()) {
    if (status.IsNotFound()) {
      return false;
    }

    return status;
  }

  {
    std::lock_guard l(config_mutex_);
    current_config_ = std::move(pb_config);
  }

  RETURN_NOT_OK(ApplyConfig(ApplyNonRuntimeAutoFlags::kTrue));

  return true;
}

Status AutoFlagsManager::LoadFromMaster(
    const string& local_hosts, const server::MasterAddresses& master_addresses,
    ApplyNonRuntimeAutoFlags apply_non_runtime) {
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

  LOG(INFO) << "Loading AutoFlags from master leader. Master addresses: [" << master_addresses_str
            << "]";

  AutoFlagsConfigPB new_config;

  const auto delay_increment =
      MonoDelta::FromMilliseconds(FLAGS_auto_flags_load_from_master_backoff_increment_ms);
  const auto max_delay_time =
      MonoDelta::FromSeconds(FLAGS_auto_flags_load_from_master_max_backoff_sec);
  auto delay_time = delay_increment;

  uint32_t attempts = 1;
  auto start_time = CoarseMonoClock::Now();
  while (true) {
    auto res = GetAutoFlagConfigFromMaster(local_hosts, master_addresses_str, *fs_manager_);
    if (res.ok()) {
      if (res->has_value()) {
        new_config = std::move(res->value());
      } else {
        // Master is running on older version which does not support AutoFlags.
        // Use a empty config. Once master leader is upgraded to a supported version, it will send
        // the new config through the WAL to other masters and through heartbeats to tservers.
        LOG(INFO) << "AutoFlags not yet initialized on master. Defaulting to empty config.";
        new_config.set_config_version(0);
      }

      break;
    }

    LOG(WARNING) << "Loading AutoFlags from master Leader failed: '" << res.status()
                 << "'. Attempts: " << attempts
                 << ", Total Time: " << CoarseMonoClock::Now() - start_time << ". Retrying...";

    // Delay before retrying so that we don't accidentally DDoS the mater.
    // Time increases linearly by delay_increment up to max_delay.
    SleepFor(delay_time);
    delay_time = std::min(max_delay_time, delay_time + delay_increment);
    attempts++;
  }

  return LoadFromConfig(std::move(new_config), apply_non_runtime);
}

Status AutoFlagsManager::LoadFromConfig(
    const AutoFlagsConfigPB new_config, ApplyNonRuntimeAutoFlags apply_non_runtime) {
  if (FLAGS_disable_auto_flags_management) {
    LOG(WARNING) << "AutoFlags management is disabled.";
    return Status::OK();
  }

  std::lock_guard update_lock(update_mutex_);

  {
    SharedLock<rw_spinlock> lock(config_mutex_);

    // First new config can be empty, and should still be written to disk.
    // Else no-op if it is the same or lower version.
    if (current_config_.config_version() != 0 &&
        new_config.config_version() <= current_config_.config_version()) {
      LOG(INFO) << "AutoFlags config update ignored as we are already on the same"
                   " or higher version. Current version: "
                << current_config_.config_version()
                << ", New version: " << new_config.config_version();
      return Status::OK();
    }
  }

  LOG(INFO) << "Storing new AutoFlags config: " << new_config.DebugString();
  RETURN_NOT_OK(fs_manager_->WriteAutoFlagsConfig(&new_config));

  {
    std::lock_guard lock(config_mutex_);
    current_config_ = std::move(new_config);
  }

  return ApplyConfig(apply_non_runtime);
}

uint32_t AutoFlagsManager::GetConfigVersion() const {
  SharedLock<rw_spinlock> lock(config_mutex_);
  return current_config_.config_version();
}

AutoFlagsConfigPB AutoFlagsManager::GetConfig() const {
  SharedLock<rw_spinlock> lock(config_mutex_);
  return current_config_;
}

Status AutoFlagsManager::ApplyConfig(ApplyNonRuntimeAutoFlags apply_non_runtime) {
  SharedLock<rw_spinlock> lock(config_mutex_);
  auto* flags = GetPerProcessFlags(process_name_, &current_config_);

  if (!flags) {
    return Status::OK();
  }

  std::vector<string> flags_to_promote;
  std::vector<string> non_runtime_flags_skipped;

  for (const auto& flag_name : *flags) {
    auto desc = GetAutoFlagDescription(flag_name);
    // This will fail if the node is running a old version of the code that does not support the
    // flag.
    RSTATUS_DCHECK(
        desc, NotSupported,
        "AutoFlag '$0' is not supported. Upgrade the process to a version that supports this flag.",
        flag_name);

    gflags::CommandLineFlagInfo flag_info;
    // All AutoFlags are gFlags, so this cannot fail.
    CHECK(GetCommandLineFlagInfo(flag_name.c_str(), &flag_info));

    if (!IsFlagPromoted(flag_info, *desc)) {
      if (apply_non_runtime || desc->is_runtime) {
        flags_to_promote.emplace_back(flag_name);
      } else {
        non_runtime_flags_skipped.emplace_back(flag_name);
      }
    }
  }

  for (const auto& flag_name : flags_to_promote) {
    // Flags have already been validated. This cannot fail.
    CHECK_OK(PromoteAutoFlag(flag_name));
  }

  if (!flags_to_promote.empty()) {
    // TODO(Hari): Its ok for this to be INFO level for now, as this is a new feature and we don't
    // have too many AutoFlags. Switch to VLOG when this assumption changes.
    LOG(INFO) << "AutoFlags applied: " << JoinStrings(flags_to_promote, ",");
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

}  // namespace yb
