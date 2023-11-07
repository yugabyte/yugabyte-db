// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
#include "yb/integration-tests/external_mini_cluster_validator.h"

#include "yb/client/client.h"
#include "yb/client/client-test-util.h"
#include "yb/client/table_info.h"

#include "yb/common/entity_ids_types.h"

#include "yb/gutil/callback.h"

#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/external_mini_cluster_fs_inspector.h"

#include "yb/master/master_client.pb.h"

#include "yb/tablet/metadata.pb.h"

#include "yb/util/async_util.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/logging.h"
#include "yb/util/monotime.h"
#include "yb/util/result.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/test_util.h"

using namespace std::chrono_literals;

namespace yb::itest {

namespace {

Status TestRetainDeleteMarkersInSysCatalog(client::YBClient* client, const TableId& table_id) {
  LOG_WITH_FUNC(INFO) << "begin";
  CHECK_NOTNULL(client);
  CHECK(!table_id.empty());

  auto table_info = std::make_shared<client::YBTableInfo>();
  {
    Synchronizer sync;
    RETURN_NOT_OK(client->GetTableSchemaById(table_id, table_info, sync.AsStatusCallback()));
    RETURN_NOT_OK(sync.Wait());
  }

  // Check value of retain_delete_markers on the master.
  SCHECK_EQ(table_info->schema.version(), 0U,
      IllegalState, Format("Unexpected schema version: $0", table_info->schema.version()));
  SCHECK(!table_info->schema.table_properties().retain_delete_markers(),
      IllegalState,
      Format("Unexpected retain_delete_markers value: $0",
          table_info->schema.table_properties().retain_delete_markers()));

  LOG_WITH_FUNC(INFO) << "end";
  return Status::OK();
}

class RetainDeleteMarkersTableValidator final {
 public:
  RetainDeleteMarkersTableValidator(
      RetainDeleteMarkersValidator* validator, const std::string& table_name)
      : validator_(*CHECK_NOTNULL(validator)), table_name_(table_name) {
  }

  Status Init();

  Result<std::pair<size_t, size_t>> GetNumPeersWithExpectedValueOnTServers(
      bool expected_retain_delete_markers);

  Status WaitForExpectedValueOnTServers(bool expected_retain_delete_markers);

  static Status Validate(
      RetainDeleteMarkersValidator* validator,
      const std::string& table_name,
      bool expected_retain_delete_markers);

 private:
  RetainDeleteMarkersValidator& validator_;
  const std::string table_name_;
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets_;
};

Status RetainDeleteMarkersTableValidator::Init() {
  LOG_WITH_FUNC(INFO) << "initializing validator for table " << table_name_;
  const auto table_id = VERIFY_RESULT(client::GetTableIdByTableName(
      &validator_.client(), validator_.namespace_name(), table_name_));
  RETURN_NOT_OK(validator_.client().GetTabletsFromTableId(table_id, 0, &tablets_));
  SCHECK_NE(tablets_.size(), 0, IllegalState,
            Format("Invalid number of tablets: $0", tablets_.size()));
  return TestRetainDeleteMarkersInSysCatalog(&validator_.client(), table_id);
}

Result<std::pair<size_t, size_t>>
RetainDeleteMarkersTableValidator::GetNumPeersWithExpectedValueOnTServers(
    bool expected_retain_delete_markers) {
  auto& cluster = validator_.cluster();
  ExternalMiniClusterFsInspector inspector { &cluster };
  size_t num_hits_total = 0;
  size_t num_hits_with_expected_value = 0;
  for (const auto& tablet : tablets_) {
    for (size_t n = 0; n < cluster.num_tablet_servers(); ++n) {
      tablet::RaftGroupReplicaSuperBlockPB superblock;
      RETURN_NOT_OK(inspector.ReadTabletSuperBlockOnTS(n, tablet.tablet_id(), &superblock));
      SCHECK(superblock.has_kv_store(), IllegalState, "");
      SCHECK_GT(superblock.kv_store().tables_size(), 0, IllegalState, "");
      for (const auto& table_pb : superblock.kv_store().tables()) {
        if (!table_pb.has_table_name() || table_pb.table_name() != table_name_) {
          continue;
        }
        SCHECK(table_pb.has_schema(), IllegalState, "");
        SCHECK(table_pb.schema().has_table_properties(), IllegalState, "");
        LOG(INFO)
            << "P " << cluster.tablet_server(n)->id() << " T " << tablet.tablet_id()
            << (table_pb.has_table_name() ? " Table " + table_pb.table_name() : "")
            << " properties: " << table_pb.schema().table_properties().ShortDebugString();
        if (table_pb.schema().table_properties().retain_delete_markers() ==
            expected_retain_delete_markers) {
          ++num_hits_with_expected_value;
        }
        ++num_hits_total;
      }
    }
  }
  return std::make_pair(num_hits_total, num_hits_with_expected_value);
}

Status RetainDeleteMarkersTableValidator::WaitForExpectedValueOnTServers(
    bool expected_retain_delete_markers) {
  return LoggedWaitFor([this, expected_retain_delete_markers]() -> Result<bool> {
    const auto num_peers = VERIFY_RESULT(
        GetNumPeersWithExpectedValueOnTServers(expected_retain_delete_markers));
    return num_peers.first == num_peers.second;
  }, 15s * kTimeMultiplier, "Wait for retain delete markers");
}

Status RetainDeleteMarkersTableValidator::Validate(RetainDeleteMarkersValidator* validator,
    const std::string& table_name, bool expected_retain_delete_markers) {
  RetainDeleteMarkersTableValidator tablet_validator{ validator, table_name };
  RETURN_NOT_OK(tablet_validator.Init());
  return tablet_validator.WaitForExpectedValueOnTServers(expected_retain_delete_markers);
}

} // namespace

RetainDeleteMarkersValidator::~RetainDeleteMarkersValidator() = default;

RetainDeleteMarkersValidator::RetainDeleteMarkersValidator(
    ExternalMiniCluster* cluster, client::YBClient* client, const std::string& namespace_name)
    : cluster_(*CHECK_NOTNULL(cluster))
    , client_(*CHECK_NOTNULL(client))
    , namespace_name_(namespace_name) {
}

Status RetainDeleteMarkersValidator::RestartCluster() {
  return cluster_.Restart();
}

void RetainDeleteMarkersValidator::Test() {
  constexpr auto kTableName = "ttt";
  constexpr auto kIndexName = "iii";
  ASSERT_OK(CreateTable(kTableName));
  ASSERT_OK(CreateIndex(kIndexName, kTableName));
  ASSERT_OK(RetainDeleteMarkersTableValidator::Validate(
      this, kIndexName, false /* expected_retain_delete_markers */));
}

void RetainDeleteMarkersValidator::TestRecovery(bool use_multiple_requests) {
  constexpr auto kValidationPeriodSec = 15;

  // Update required flags with cluster restart -- it's required for non-runtime flags.
  cluster_.AddExtraFlagOnTServers("vmodule", "tablet_validator=1");
  cluster_.Shutdown(ExternalMiniCluster::TS_ONLY);
  cluster_.AddExtraFlagOnTServers(
      "tablet_validator_retain_delete_markers_validation_period_sec",
      std::to_string(kValidationPeriodSec));
  if (use_multiple_requests) {
    cluster_.AddExtraFlagOnTServers("TEST_tablet_validator_max_tables_number_per_rpc", "1");
    cluster_.AddExtraFlagOnTServers("TEST_tablet_validator_one_rpc_per_validation_period", "true");
  }

  // Simulate the situaion where retain_delete_markers are not set properly on a tserver.
  cluster_.AddExtraFlagOnTServers("TEST_skip_metadata_backfill_done", "true");
  ASSERT_OK(RestartCluster());

  // Create several tables and indexes
  const std::vector<std::pair<std::string, std::string>> indexes {
      {"idx11", "ttt11"}, {"idx21", "ttt21"}, {"idx22", "ttt22"}, {"idx31", "ttt31"}
  };
  std::unordered_set<std::string> tables;
  for (const auto& [index_name, table_name] : indexes) {
    if (!tables.contains(table_name)) {
      ASSERT_OK(CreateTable(table_name));
    }
    ASSERT_OK(CreateIndex(index_name, table_name));
  }

  // Give some time for tserver to receive BackfillDone (and make sure retain_delete_markers
  // update has been ignored).
  std::this_thread::sleep_for(5s * kTimeMultiplier);
  for (const auto& [index_name, _] : indexes) {
    ASSERT_OK(RetainDeleteMarkersTableValidator::Validate(
      this, index_name, true /* expected_retain_delete_markers */));
  }

  // Allow retain_delet_markers to be updated correctly on a tserver.
  cluster_.Shutdown(ExternalMiniCluster::TS_ONLY);
  cluster_.RemoveExtraFlagOnTServers("TEST_skip_metadata_backfill");
  ASSERT_OK(RestartCluster());

  if (!use_multiple_requests) {
    // Give some time for the remaining tables recovery
    std::this_thread::sleep_for(1s * kValidationPeriodSec * indexes.size());

    // Start a separate thread for each index to spend less time during validation.
    TestThreadHolder holder;
    for (const auto& [index_name, _] : indexes) {
      holder.AddThreadFunctor([this, &name = index_name]() {
        ASSERT_OK(RetainDeleteMarkersTableValidator::Validate(
          this, name, false /* expected_retain_delete_markers */));
      });
    }
    holder.JoinAll();
    return;
  }

  // Give some time for the recovery.
  std::this_thread::sleep_for(1s * kValidationPeriodSec);
  const auto num_remaining_sec = 1s * kValidationPeriodSec * (indexes.size() - 1);

  LOG(INFO) << "Checking retain_delete_markers state";

  std::atomic<size_t> num_restored {0};
  TestThreadHolder holder;
  for (const auto& [index_name, _] : indexes) {
    holder.AddThreadFunctor([this, num_remaining_sec, &num_restored, &name = index_name]() {
      RetainDeleteMarkersTableValidator tablet_validator{ this, name };
      ASSERT_OK(tablet_validator.Init());

      auto num_peers = ASSERT_RESULT(tablet_validator.GetNumPeersWithExpectedValueOnTServers(
        false /* expected_value_on_tservers */));
      LOG(INFO) << "Index table " << name << " numbers: " << yb::ToString(num_peers);
      if (num_peers.first == num_peers.second) {
        num_restored.fetch_add(1, std::memory_order_relaxed);
      }

      // Check the state has been recovered.
      std::this_thread::sleep_for(num_remaining_sec);
      ASSERT_OK(tablet_validator.WaitForExpectedValueOnTServers(
          false /* expected_value_on_tservers */));
    });
  }
  holder.JoinAll();

  // TODO(#19731): yb.master.MasterService.ListTables takes too much time if TSAN builds and such
  // behaviour breaks the logic of the tests. That's why a different check is used. Maybe such
  // behaviour should be additionally investigated.
  if constexpr (!IsTsan()) {
    ASSERT_EQ(num_restored.load(), 1);
  } else {
    ASSERT_GE(num_restored.load(), 1);
    ASSERT_LT(num_restored.load(), indexes.size()); // Could still be unstable.
  }
}

} // namespace yb::itest
