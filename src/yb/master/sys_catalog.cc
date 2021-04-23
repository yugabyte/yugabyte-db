// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/master/sys_catalog.h"

#include <cmath>
#include <memory>
#include <rapidjson/document.h>

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <boost/optional.hpp>

#include "yb/common/partial_row.h"
#include "yb/common/partition.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"
#include "yb/common/ql_value.h"
#include "yb/common/ql_protocol_util.h"

#include "yb/consensus/log_anchor_registry.h"
#include "yb/consensus/consensus.h"
#include "yb/consensus/consensus_meta.h"
#include "yb/consensus/consensus_peers.h"
#include "yb/consensus/opid_util.h"
#include "yb/consensus/quorum_util.h"

#include "yb/docdb/doc_rowwise_iterator.h"
#include "yb/docdb/docdb_pgapi.h"
#include "yb/docdb/docdb_rocksdb_util.h"

#include "yb/fs/fs_manager.h"
#include "yb/gutil/strings/split.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/master.pb.h"
#include "yb/master/sys_catalog_writer.h"
#include "yb/rpc/rpc_context.h"
#include "yb/tablet/operations/write_operation.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_bootstrap_if.h"
#include "yb/tablet/tablet_fwd.h"
#include "yb/tablet/tablet_options.h"

#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/flag_tags.h"
#include "yb/util/logging.h"
#include "yb/util/net/dns_resolver.h"
#include "yb/util/random_util.h"
#include "yb/util/size_literals.h"
#include "yb/util/string_util.h"
#include "yb/util/threadpool.h"

using namespace std::literals; // NOLINT
using namespace yb::size_literals;

using std::shared_ptr;
using std::unique_ptr;

using yb::consensus::CONSENSUS_CONFIG_ACTIVE;
using yb::consensus::CONSENSUS_CONFIG_COMMITTED;
using yb::consensus::ConsensusMetadata;
using yb::consensus::RaftConfigPB;
using yb::consensus::RaftPeerPB;
using yb::log::Log;
using yb::log::LogAnchorRegistry;
using yb::tserver::WriteRequestPB;
using yb::tserver::WriteResponsePB;
using strings::Substitute;
using yb::consensus::StateChangeContext;
using yb::consensus::StateChangeReason;
using yb::consensus::ChangeConfigRequestPB;
using yb::consensus::ChangeConfigRecordPB;

DEFINE_bool(notify_peer_of_removal_from_cluster, true,
            "Notify a peer after it has been removed from the cluster.");
TAG_FLAG(notify_peer_of_removal_from_cluster, hidden);
TAG_FLAG(notify_peer_of_removal_from_cluster, advanced);

METRIC_DEFINE_histogram(
  server, dns_resolve_latency_during_sys_catalog_setup,
  "yb.master.SysCatalogTable.SetupConfig DNS Resolve",
  yb::MetricUnit::kMicroseconds,
  "Microseconds spent resolving DNS requests during SysCatalogTable::SetupConfig",
  60000000LU, 2);
METRIC_DEFINE_counter(
  server, sys_catalog_peer_write_count,
  "yb.master.SysCatalogTable Count of Writes",
  yb::MetricUnit::kRequests,
  "Number of writes to disk handled by the system catalog.");

DECLARE_int32(master_discovery_timeout_ms);

DEFINE_int32(sys_catalog_write_timeout_ms, 60000, "Timeout for writes into system catalog");
DEFINE_int32(copy_tables_batch_bytes, 500_KB, "Max bytes per batch for copy pg sql tables");

DEFINE_test_flag(int32, sys_catalog_write_rejection_percentage, 0,
  "Reject specified percentage of sys catalog writes.");

namespace yb {
namespace master {

constexpr int32_t kDefaultMasterBlockCacheSizePercentage = 25;

std::string SysCatalogTable::schema_column_type() { return kSysCatalogTableColType; }

std::string SysCatalogTable::schema_column_id() { return kSysCatalogTableColId; }

std::string SysCatalogTable::schema_column_metadata() { return kSysCatalogTableColMetadata; }

SysCatalogTable::SysCatalogTable(Master* master, MetricRegistry* metrics,
                                 ElectedLeaderCallback leader_cb)
    : schema_(BuildTableSchema()),
      metric_registry_(metrics),
      metric_entity_(METRIC_ENTITY_server.Instantiate(metric_registry_, "yb.master")),
      master_(master),
      leader_cb_(std::move(leader_cb)) {
  CHECK_OK(ThreadPoolBuilder("inform_removed_master").Build(&inform_removed_master_pool_));
  CHECK_OK(ThreadPoolBuilder("raft").Build(&raft_pool_));
  CHECK_OK(ThreadPoolBuilder("prepare").set_min_threads(1).Build(&tablet_prepare_pool_));
  CHECK_OK(ThreadPoolBuilder("append").set_min_threads(1).Build(&append_pool_));
  CHECK_OK(ThreadPoolBuilder("log-alloc").set_min_threads(1).Build(&allocation_pool_));

  setup_config_dns_histogram_ = METRIC_dns_resolve_latency_during_sys_catalog_setup.Instantiate(
      metric_entity_);
  peer_write_count = METRIC_sys_catalog_peer_write_count.Instantiate(metric_entity_);
}

SysCatalogTable::~SysCatalogTable() {
}

void SysCatalogTable::StartShutdown() {
  auto peer = tablet_peer();
  if (peer) {
    CHECK(peer->StartShutdown());
  }
}

void SysCatalogTable::CompleteShutdown() {
  auto peer = tablet_peer();
  if (peer) {
    peer->CompleteShutdown();
  }
  inform_removed_master_pool_->Shutdown();
  raft_pool_->Shutdown();
  tablet_prepare_pool_->Shutdown();
}

Status SysCatalogTable::ConvertConfigToMasterAddresses(
    const RaftConfigPB& config,
    bool check_missing_uuids) {
  auto loaded_master_addresses = std::make_shared<server::MasterAddresses>();
  bool has_missing_uuids = false;
  for (const auto& peer : config.peers()) {
    if (check_missing_uuids && !peer.has_permanent_uuid()) {
      LOG(WARNING) << "No uuid for master peer: " << peer.ShortDebugString();
      has_missing_uuids = true;
      break;
    }

    loaded_master_addresses->push_back({});
    auto& list = loaded_master_addresses->back();
    for (const auto& hp : peer.last_known_private_addr()) {
      list.push_back(HostPortFromPB(hp));
    }
    for (const auto& hp : peer.last_known_broadcast_addr()) {
      list.push_back(HostPortFromPB(hp));
    }
  }

  if (has_missing_uuids) {
    return STATUS(IllegalState, "Trying to load distributed config, but had missing uuids.");
  }

  master_->SetMasterAddresses(loaded_master_addresses);

  return Status::OK();
}

Status SysCatalogTable::CreateAndFlushConsensusMeta(
    FsManager* fs_manager,
    const RaftConfigPB& config,
    int64_t current_term) {
  std::unique_ptr<ConsensusMetadata> cmeta;
  string tablet_id = kSysCatalogTabletId;
  RETURN_NOT_OK_PREPEND(ConsensusMetadata::Create(fs_manager,
                                                  tablet_id,
                                                  fs_manager->uuid(),
                                                  config,
                                                  current_term,
                                                  &cmeta),
                        "Unable to persist consensus metadata for tablet " + tablet_id);
  return Status::OK();
}

Status SysCatalogTable::Load(FsManager* fs_manager) {
  LOG(INFO) << "Trying to load previous SysCatalogTable data from disk";
  // Load Metadata Information from disk
  auto metadata = VERIFY_RESULT(tablet::RaftGroupMetadata::Load(fs_manager, kSysCatalogTabletId));

  // Verify that the schema is the current one
  if (!metadata->schema()->Equals(schema_)) {
    // TODO: In this case we probably should execute the migration step.
    return(STATUS(Corruption, "Unexpected schema", metadata->schema()->ToString()));
  }

  // Update partition schema of old SysCatalogTable. SysCatalogTable should be non-partitioned.
  if (metadata->partition_schema()->IsHashPartitioning()) {
    LOG(INFO) << "Updating partition schema of SysCatalogTable ...";
    PartitionSchema partition_schema;
    RETURN_NOT_OK(PartitionSchema::FromPB(PartitionSchemaPB(), *metadata->schema(),
                                          &partition_schema));
    metadata->SetPartitionSchema(partition_schema);
    RETURN_NOT_OK(metadata->Flush());
  }

  // TODO(bogdan) we should revisit this as well as next step to understand what happens if you
  // started on this local config, but the consensus layer has a different config? (essentially,
  // if your local cmeta is stale...
  //
  // Allow for statically and explicitly assigning the consensus configuration and roles through
  // the master configuration on startup.
  //
  // TODO: The following assumptions need revisiting:
  // 1. We always believe the local config options for who is in the consensus configuration.
  // 2. We always want to look up all node's UUIDs on start (via RPC).
  //    - TODO: Cache UUIDs. See KUDU-526.
  string tablet_id = metadata->raft_group_id();
  std::unique_ptr<ConsensusMetadata> cmeta;
  RETURN_NOT_OK_PREPEND(ConsensusMetadata::Load(fs_manager, tablet_id, fs_manager->uuid(), &cmeta),
                        "Unable to load consensus metadata for tablet " + tablet_id);

  const RaftConfigPB& loaded_config = cmeta->active_config();
  DCHECK(!loaded_config.peers().empty()) << "Loaded consensus metadata, but had no peers!";

  if (loaded_config.peers().empty()) {
    return STATUS(IllegalState, "Trying to load distributed config, but contains no peers.");
  }

  if (loaded_config.peers().size() > 1) {
    LOG(INFO) << "Configuring consensus for distributed operation...";
    RETURN_NOT_OK(ConvertConfigToMasterAddresses(loaded_config, true));
  } else {
    LOG(INFO) << "Configuring consensus for local operation...";
    // We know we have exactly one peer.
    const auto& peer = loaded_config.peers().Get(0);
    if (!peer.has_permanent_uuid()) {
      return STATUS(IllegalState, "Loaded consesnsus metadata, but peer did not have a uuid");
    }
    if (peer.permanent_uuid() != fs_manager->uuid()) {
      return STATUS(IllegalState, Substitute(
          "Loaded consensus metadata, but peer uuid ($0) was different than our uuid ($1)",
          peer.permanent_uuid(), fs_manager->uuid()));
    }
  }

  RETURN_NOT_OK(SetupTablet(metadata));
  return Status::OK();
}

Status SysCatalogTable::CreateNew(FsManager *fs_manager) {
  LOG(INFO) << "Creating new SysCatalogTable data";
  // Create the new Metadata
  Schema schema = BuildTableSchema();
  PartitionSchema partition_schema;
  RETURN_NOT_OK(PartitionSchema::FromPB(PartitionSchemaPB(), schema, &partition_schema));

  vector<YBPartialRow> split_rows;
  vector<Partition> partitions;
  RETURN_NOT_OK(partition_schema.CreatePartitions(split_rows, schema, &partitions));
  DCHECK_EQ(1, partitions.size());

  auto table_info = std::make_shared<tablet::TableInfo>(
      kSysCatalogTableId, "", table_name(), TableType::YQL_TABLE_TYPE, schema, IndexMap(),
      boost::none /* index_info */, 0 /* schema_version */, partition_schema);
  auto metadata = VERIFY_RESULT(tablet::RaftGroupMetadata::CreateNew(tablet::RaftGroupMetadataData {
    .fs_manager = fs_manager,
    .table_info = table_info,
    .raft_group_id = kSysCatalogTabletId,
    .partition = partitions[0],
    .tablet_data_state = tablet::TABLET_DATA_READY,
  }));

  RaftConfigPB config;
  RETURN_NOT_OK_PREPEND(SetupConfig(master_->opts(), &config),
                        "Failed to initialize distributed config");

  RETURN_NOT_OK(CreateAndFlushConsensusMeta(fs_manager, config, consensus::kMinimumTerm));

  return SetupTablet(metadata);
}

Status SysCatalogTable::SetupConfig(const MasterOptions& options,
                                    RaftConfigPB* committed_config) {
  // Build the set of followers from our server options.
  auto master_addresses = options.GetMasterAddresses();  // ENG-285

  // Now resolve UUIDs.
  // By the time a SysCatalogTable is created and initted, the masters should be
  // starting up, so this should be fine to do.
  DCHECK(master_->messenger());
  RaftConfigPB resolved_config;
  resolved_config.set_opid_index(consensus::kInvalidOpIdIndex);

  ScopedDnsTracker dns_tracker(setup_config_dns_histogram_);
  for (const auto& list : *options.GetMasterAddresses()) {
    LOG(INFO) << "Determining permanent_uuid for " + yb::ToString(list);
    RaftPeerPB new_peer;
    // TODO: Use ConsensusMetadata to cache the results of these lookups so
    // we only require RPC access to the full consensus configuration on first startup.
    // See KUDU-526.
    RETURN_NOT_OK_PREPEND(
      consensus::SetPermanentUuidForRemotePeer(
        &master_->proxy_cache(),
        std::chrono::milliseconds(FLAGS_master_discovery_timeout_ms),
        list,
        &new_peer),
      Format("Unable to resolve UUID for $0", yb::ToString(list)));
    resolved_config.add_peers()->Swap(&new_peer);
  }

  LOG(INFO) << "Setting up raft configuration: " << resolved_config.ShortDebugString();

  RETURN_NOT_OK(consensus::VerifyRaftConfig(resolved_config, consensus::COMMITTED_QUORUM));

  *committed_config = resolved_config;
  return Status::OK();
}

void SysCatalogTable::SysCatalogStateChanged(
    const string& tablet_id,
    std::shared_ptr<StateChangeContext> context) {
  CHECK_EQ(tablet_id, tablet_peer()->tablet_id());
  shared_ptr<consensus::Consensus> consensus = tablet_peer()->shared_consensus();
  if (!consensus) {
    LOG_WITH_PREFIX(WARNING) << "Received notification of tablet state change "
                             << "but tablet no longer running. Tablet ID: "
                             << tablet_id << ". Reason: " << context->ToString();
    return;
  }

  // We use the active config, in case there is a pending one with this peer becoming the voter,
  // that allows its role to be determined correctly as the LEADER and so loads the sys catalog.
  // Done as part of ENG-286.
  consensus::ConsensusStatePB cstate = context->is_config_locked() ?
      consensus->ConsensusStateUnlocked(CONSENSUS_CONFIG_ACTIVE) :
      consensus->ConsensusState(CONSENSUS_CONFIG_ACTIVE);
  LOG_WITH_PREFIX(INFO) << "SysCatalogTable state changed. Locked=" << context->is_config_locked_
                        << ". Reason: " << context->ToString()
                        << ". Latest consensus state: " << cstate.ShortDebugString();
  RaftPeerPB::Role role = GetConsensusRole(tablet_peer()->permanent_uuid(), cstate);
  LOG_WITH_PREFIX(INFO) << "This master's current role is: "
                        << RaftPeerPB::Role_Name(role);

  // For LEADER election case only, load the sysCatalog into memory via the callback.
  // Note that for a *single* master case, the TABLET_PEER_START is being overloaded to imply a
  // leader creation step, as there is no election done per-se.
  // For the change config case, LEADER is the one which started the operation, so new role is same
  // as its old role of LEADER and hence it need not reload the sysCatalog via the callback.
  if (role == RaftPeerPB::LEADER &&
      (context->reason == StateChangeReason::NEW_LEADER_ELECTED ||
       (cstate.config().peers_size() == 1 &&
        context->reason == StateChangeReason::TABLET_PEER_STARTED))) {
    CHECK_OK(leader_cb_.Run());
  }

  // Perform any further changes for context based reasons.
  // For config change peer update, both leader and follower need to update their in-memory state.
  // NOTE: if there are any errors, we check in debug mode, but ignore the error in non-debug case.
  if (context->reason == StateChangeReason::LEADER_CONFIG_CHANGE_COMPLETE ||
      context->reason == StateChangeReason::FOLLOWER_CONFIG_CHANGE_COMPLETE) {
    int new_count = context->change_record.new_config().peers_size();
    int old_count = context->change_record.old_config().peers_size();

    LOG(INFO) << "Processing context '" << context->ToString()
              << "' - new count " << new_count << ", old count " << old_count;

    // If new_config and old_config have the same number of peers, then the change config must have
    // been a ROLE_CHANGE, thus old_config must have exactly one peer in transition (PRE_VOTER or
    // PRE_OBSERVER) and new_config should have none.
    if (new_count == old_count) {
      int old_config_peers_transition_count =
          CountServersInTransition(context->change_record.old_config());
      if ( old_config_peers_transition_count != 1) {
        LOG(FATAL) << "Expected old config to have one server in transition (PRE_VOTER or "
                   << "PRE_OBSERVER), but found " << old_config_peers_transition_count
                   << ". Config: " << context->change_record.old_config().ShortDebugString();
      }
      int new_config_peers_transition_count =
          CountServersInTransition(context->change_record.new_config());
      if (new_config_peers_transition_count != 0) {
        LOG(FATAL) << "Expected new config to have no servers in transition (PRE_VOTER or "
                   << "PRE_OBSERVER), but found " << new_config_peers_transition_count
                   << ". Config: " << context->change_record.old_config().ShortDebugString();
      }
    } else if (std::abs(new_count - old_count) != 1) {

      LOG(FATAL) << "Expected exactly one server addition or deletion, found " << new_count
                 << " servers in new config and " << old_count << " servers in old config.";
      return;
    }

    Status s = master_->ResetMemoryState(context->change_record.new_config());
    if (!s.ok()) {
      LOG(WARNING) << "Change Memory state failed " << s.ToString();
      DCHECK(false);
      return;
    }

    // Try to make the removed master, go back to shell mode so as not to ping this cluster.
    // This is best effort and should not perform any fatals or checks.
    if (FLAGS_notify_peer_of_removal_from_cluster &&
        context->reason == StateChangeReason::LEADER_CONFIG_CHANGE_COMPLETE &&
        context->remove_uuid != "") {
      RaftPeerPB peer;
      LOG(INFO) << "Asking " << context->remove_uuid << " to go into shell mode";
      WARN_NOT_OK(GetRaftConfigMember(context->change_record.old_config(),
                                      context->remove_uuid,
                                      &peer),
                  Substitute("Could not find uuid=$0 in config.", context->remove_uuid));
      WARN_NOT_OK(
          inform_removed_master_pool_->SubmitFunc(
              [this, host_port = DesiredHostPort(peer, master_->MakeCloudInfoPB())]() {
            WARN_NOT_OK(master_->InformRemovedMaster(host_port),
                        "Failed to inform removed master " + host_port.ShortDebugString());
          }),
          Substitute("Error submitting removal task for uuid=$0", context->remove_uuid));
    }
  } else {
    VLOG(2) << "Reason '" << context->ToString() << "' provided in state change context, "
            << "no action needed.";
  }
}

Status SysCatalogTable::GoIntoShellMode() {
  CHECK(tablet_peer());
  StartShutdown();
  CompleteShutdown();

  // Remove on-disk log, cmeta and tablet superblocks.
  RETURN_NOT_OK(tserver::DeleteTabletData(tablet_peer()->tablet_metadata(),
                                          tablet::TABLET_DATA_DELETED,
                                          master_->fs_manager()->uuid(),
                                          yb::OpId()));
  RETURN_NOT_OK(tablet_peer()->tablet_metadata()->DeleteSuperBlock());
  RETURN_NOT_OK(master_->fs_manager()->DeleteFileSystemLayout());
  std::shared_ptr<tablet::TabletPeer> null_tablet_peer(nullptr);
  std::atomic_store(&tablet_peer_, null_tablet_peer);
  inform_removed_master_pool_.reset();
  raft_pool_.reset();
  tablet_prepare_pool_.reset();

  return Status::OK();
}

void SysCatalogTable::SetupTabletPeer(const scoped_refptr<tablet::RaftGroupMetadata>& metadata) {
  InitLocalRaftPeerPB();

  // TODO: handle crash mid-creation of tablet? do we ever end up with a
  // partially created tablet here?
  auto tablet_peer = std::make_shared<tablet::TabletPeer>(
      metadata,
      local_peer_pb_,
      scoped_refptr<server::Clock>(master_->clock()),
      metadata->fs_manager()->uuid(),
      Bind(&SysCatalogTable::SysCatalogStateChanged, Unretained(this), metadata->raft_group_id()),
      metric_registry_,
      nullptr /* tablet_splitter */,
      master_->async_client_initializer().get_client_future());

  std::atomic_store(&tablet_peer_, tablet_peer);
}

Status SysCatalogTable::SetupTablet(const scoped_refptr<tablet::RaftGroupMetadata>& metadata) {
  SetupTabletPeer(metadata);

  RETURN_NOT_OK(OpenTablet(metadata));

  return Status::OK();
}

Status SysCatalogTable::OpenTablet(const scoped_refptr<tablet::RaftGroupMetadata>& metadata) {
  CHECK(tablet_peer());

  tablet::TabletPtr tablet;
  scoped_refptr<Log> log;
  consensus::ConsensusBootstrapInfo consensus_info;
  RETURN_NOT_OK(tablet_peer()->SetBootstrapping());
  tablet::TabletOptions tablet_options;

  block_based_table_mem_tracker_ = docdb::InitBlockCacheMemTracker(
      kDefaultMasterBlockCacheSizePercentage,
      master_->mem_tracker());
  block_based_table_gc_ = docdb::InitBlockCache(
      GetMetricEntity(),
      kDefaultMasterBlockCacheSizePercentage,
      block_based_table_mem_tracker_.get(),
      &tablet_options);

  tablet::TabletInitData tablet_init_data = {
      .metadata = metadata,
      .client_future = master_->async_client_initializer().get_client_future(),
      .clock = scoped_refptr<server::Clock>(master_->clock()),
      .parent_mem_tracker = master_->mem_tracker(),
      .block_based_table_mem_tracker = block_based_table_mem_tracker_,
      .metric_registry = metric_registry_,
      .log_anchor_registry = tablet_peer()->log_anchor_registry(),
      .tablet_options = tablet_options,
      .log_prefix_suffix = " P " + tablet_peer()->permanent_uuid(),
      .transaction_participant_context = tablet_peer().get(),
      .local_tablet_filter = client::LocalTabletFilter(),
      // This is only required if the sys catalog tablet is also acting as a transaction status
      // tablet, which it does not as of 12/06/2019. This could have been a nullptr, but putting
      // the TabletPeer here in case we need this for rolling master upgrades when we do enable
      // storing transaction status records in the sys catalog tablet.
      .transaction_coordinator_context = tablet_peer().get(),
      // Disable transactions if we are creating the initial sys catalog snapshot.
      // initdb is much faster with transactions disabled.
      .txns_enabled = tablet::TransactionsEnabled(!FLAGS_create_initial_sys_catalog_snapshot),
      .is_sys_catalog = tablet::IsSysCatalogTablet::kTrue,
      .snapshot_coordinator = &master_->catalog_manager()->snapshot_coordinator(),
      .tablet_splitter = nullptr,
  };
  tablet::BootstrapTabletData data = {
      .tablet_init_data = tablet_init_data,
      .listener = tablet_peer()->status_listener(),
      .append_pool = append_pool(),
      .allocation_pool = allocation_pool_.get(),
      .retryable_requests = nullptr,
  };
  RETURN_NOT_OK(BootstrapTablet(data, &tablet, &log, &consensus_info));

  // TODO: Do we have a setSplittable(false) or something from the outside is
  // handling split in the TS?

  RETURN_NOT_OK_PREPEND(
      tablet_peer()->InitTabletPeer(
          tablet,
          master_->mem_tracker(),
          master_->messenger(),
          &master_->proxy_cache(),
          log,
          tablet->GetTableMetricsEntity(),
          tablet->GetTabletMetricsEntity(),
          raft_pool(),
          tablet_prepare_pool(),
          nullptr /* retryable_requests */,
          consensus::SplitOpInfo()),
      "Failed to Init() TabletPeer");

  RETURN_NOT_OK_PREPEND(tablet_peer()->Start(consensus_info),
                        "Failed to Start() TabletPeer");

  tablet_peer()->RegisterMaintenanceOps(master_->maintenance_manager());

  if (!tablet->schema()->Equals(schema_)) {
    return STATUS(Corruption, "Unexpected schema", tablet->schema()->ToString());
  }
  return Status::OK();
}

std::string SysCatalogTable::LogPrefix() const {
  return Substitute("T $0 P $1 [$2]: ",
                    tablet_peer()->tablet_id(),
                    tablet_peer()->permanent_uuid(),
                    table_name());
}

Status SysCatalogTable::WaitUntilRunning() {
  TRACE_EVENT0("master", "SysCatalogTable::WaitUntilRunning");
  int seconds_waited = 0;
  while (true) {
    Status status = tablet_peer()->WaitUntilConsensusRunning(MonoDelta::FromSeconds(1));
    seconds_waited++;
    if (status.ok()) {
      LOG_WITH_PREFIX(INFO) << "configured and running, proceeding with master startup.";
      break;
    }
    if (status.IsTimedOut()) {
      LOG_WITH_PREFIX(INFO) <<  "not online yet (have been trying for "
                               << seconds_waited << " seconds)";
      continue;
    }
    // if the status is not OK or TimedOut return it.
    return status;
  }
  return Status::OK();
}

CHECKED_STATUS SysCatalogTable::SyncWrite(SysCatalogWriter* writer) {
  if (PREDICT_FALSE(FLAGS_TEST_sys_catalog_write_rejection_percentage > 0) &&
      RandomUniformInt(1, 99) <= FLAGS_TEST_sys_catalog_write_rejection_percentage) {
    return STATUS(InternalError, "Injected random failure for testing.");
  }

  auto resp = std::make_shared<tserver::WriteResponsePB>();
  // If this is a PG write, them the pgsql write batch is not empty.
  //
  // If this is a QL write, then it is a normal sys_catalog write, so ignore writes that might
  // have filtered out all of the writes from the batch, as they were the same payload as the cow
  // objects that are backing them.
  if (writer->req().ql_write_batch().empty() && writer->req().pgsql_write_batch().empty()) {
    return Status::OK();
  }

  auto latch = std::make_shared<CountDownLatch>(1);
  auto operation_state = std::make_unique<tablet::WriteOperationState>(
      tablet_peer()->tablet(), &writer->req(), resp.get());
  operation_state->set_completion_callback(
      tablet::MakeLatchOperationCompletionCallback(latch, resp));

  tablet_peer()->WriteAsync(
      std::move(operation_state), writer->leader_term(), CoarseTimePoint::max() /* deadline */);
  peer_write_count->Increment();

  {
    int num_iterations = 0;
    auto time = CoarseMonoClock::now();
    auto deadline = time + FLAGS_sys_catalog_write_timeout_ms * 1ms;
    static constexpr auto kWarningInterval = 5s;
    while (!latch->WaitUntil(std::min(deadline, time + kWarningInterval))) {
      ++num_iterations;
      const auto waited_so_far = num_iterations * kWarningInterval;
      LOG(WARNING) << "Waited for " << AsString(waited_so_far) << " for synchronous write to "
                   << "complete. Continuing to wait.";
      time = CoarseMonoClock::now();
      if (time >= deadline) {
        LOG(ERROR) << "Already waited for a total of " << ::yb::ToString(waited_so_far) << ". "
                   << "Returning a timeout from SyncWrite.";
        return STATUS_FORMAT(TimedOut, "SyncWrite timed out after $0", waited_so_far);
      }
    }
  }

  if (resp->has_error()) {
    return StatusFromPB(resp->error().status());
  }
  if (resp->per_row_errors_size() > 0) {
    for (const WriteResponsePB::PerRowErrorPB& error : resp->per_row_errors()) {
      LOG(WARNING) << "row " << error.row_index() << ": " << StatusFromPB(error.error()).ToString();
    }
    return STATUS(Corruption, "One or more rows failed to write");
  }
  return Status::OK();
}

// Schema for the unified SysCatalogTable:
//
// (entry_type, entry_id) -> metadata
//
// entry_type is a enum defined in sys_tables. It indicates
// whether an entry is a table or a tablet.
//
// entry_type is the first part of a compound key as to allow
// efficient scans of entries of only a single type (e.g., only
// scan all of the tables, or only scan all of the tablets).
//
// entry_id is either a table id or a tablet id. For tablet entries,
// the table id that the tablet is associated with is stored in the
// protobuf itself.
Schema SysCatalogTable::BuildTableSchema() {
  SchemaBuilder builder;
  CHECK_OK(builder.AddKeyColumn(kSysCatalogTableColType, INT8));
  CHECK_OK(builder.AddKeyColumn(kSysCatalogTableColId, BINARY));
  CHECK_OK(builder.AddColumn(kSysCatalogTableColMetadata, BINARY));
  return builder.Build();
}

// ==================================================================
// Other methods
// ==================================================================
void SysCatalogTable::InitLocalRaftPeerPB() {
  local_peer_pb_.set_permanent_uuid(master_->fs_manager()->uuid());
  ServerRegistrationPB reg;
  CHECK_OK(master_->GetRegistration(&reg, server::RpcOnly::kTrue));
  TakeRegistration(&reg, &local_peer_pb_);
}

Status SysCatalogTable::Visit(VisitorBase* visitor) {
  TRACE_EVENT0("master", "Visitor::VisitAll");

  auto tablet = tablet_peer()->shared_tablet();
  if (!tablet) {
    return STATUS(ShutdownInProgress, "SysConfig is shutting down.");
  }

  auto start = CoarseMonoClock::Now();

  uint64_t count = 0;
  RETURN_NOT_OK(EnumerateSysCatalog(tablet.get(), schema_, visitor->entry_type(),
                                    [visitor, &count](const Slice& id, const Slice& data) {
    ++count;
    return visitor->Visit(id, data);
  }));

  auto duration = CoarseMonoClock::Now() - start;
  string id = Format("num_entries_with_type_$0_loaded", std::to_string(visitor->entry_type()));
  if (visitor_duration_metrics_.find(id) == visitor_duration_metrics_.end()) {
    string description = id + " metric for SysCatalogTable::Visit";
    std::unique_ptr<GaugePrototype<uint64>> counter_gauge =
        std::make_unique<OwningGaugePrototype<uint64>>(
            "server", id, description, yb::MetricUnit::kEntries, description,
            yb::MetricLevel::kInfo, yb::EXPOSE_AS_COUNTER);
    visitor_duration_metrics_[id] = metric_entity_->FindOrCreateGauge(
        std::move(counter_gauge), static_cast<uint64>(0) /* initial_value */);
  }
  visitor_duration_metrics_[id]->IncrementBy(count);

  id = Format("duration_ms_loading_entries_with_type_$0", std::to_string(visitor->entry_type()));
  if (visitor_duration_metrics_.find(id) == visitor_duration_metrics_.end()) {
    string description = id + " metric for SysCatalogTable::Visit";
    std::unique_ptr<GaugePrototype<uint64>> duration_gauge =
        std::make_unique<OwningGaugePrototype<uint64>>(
            "server", id, description, yb::MetricUnit::kMilliseconds, description,
            yb::MetricLevel::kInfo);
    visitor_duration_metrics_[id] = metric_entity_->FindOrCreateGauge(
        std::move(duration_gauge), static_cast<uint64>(0) /* initial_value */);
  }
  visitor_duration_metrics_[id]->IncrementBy(ToMilliseconds(duration));
  return Status::OK();
}

Status SysCatalogTable::ReadYsqlCatalogVersion(TableId ysql_catalog_table_id,
                                               uint64_t *catalog_version,
                                               uint64_t *last_breaking_version) {
  TRACE_EVENT0("master", "ReadYsqlCatalogVersion");
  const tablet::TabletPtr tablet = tablet_peer()->shared_tablet();
  const auto* meta = tablet->metadata();
  const std::shared_ptr<tablet::TableInfo> ysql_catalog_table_info =
      VERIFY_RESULT(meta->GetTableInfo(ysql_catalog_table_id));
  const Schema& schema = ysql_catalog_table_info->schema;
  auto iter = VERIFY_RESULT(tablet->NewRowIterator(schema.CopyWithoutColumnIds(),
                                                   boost::none /* transaction_id */,
                                                   {} /* read_hybrid_time */,
                                                   ysql_catalog_table_id));
  QLTableRow source_row;
  ColumnId version_col_id = VERIFY_RESULT(schema.ColumnIdByName("current_version"));
  ColumnId last_breaking_version_col_id =
      VERIFY_RESULT(schema.ColumnIdByName("last_breaking_version"));

  if (VERIFY_RESULT(iter->HasNext())) {
    RETURN_NOT_OK(iter->NextRow(&source_row));
    if (catalog_version) {
      auto version_col_value = source_row.GetValue(version_col_id);
      if (version_col_value) {
        *catalog_version = version_col_value->int64_value();
      } else {
        return STATUS(Corruption, "Could not read syscatalog version");
      }
    }
    // last_breaking_version is the last version (change) that invalidated ongoing transactions.
    if (last_breaking_version) {
      auto last_breaking_version_col_value = source_row.GetValue(last_breaking_version_col_id);
      if (last_breaking_version_col_value) {
        *last_breaking_version = last_breaking_version_col_value->int64_value();
      } else {
        return STATUS(Corruption, "Could not read syscatalog version");
      }
    }
  } else {
    // If no row it means version is 0 (not initialized yet).
    if (catalog_version) {
      *catalog_version = 0;
    }
    if (last_breaking_version) {
      *last_breaking_version = 0;
    }
  }

  return Status::OK();
}

Result<shared_ptr<TablespaceIdToReplicationInfoMap>> SysCatalogTable::ReadPgTablespaceInfo() {
  TRACE_EVENT0("master", "ReadPgTablespaceInfo");

  const tablet::TabletPtr tablet = tablet_peer()->shared_tablet();

  const auto& pg_tablespace_info =
      VERIFY_RESULT(tablet->metadata()->GetTableInfo(kPgTablespaceTableId));
  const Schema& schema = pg_tablespace_info->schema;
  auto iter = VERIFY_RESULT(tablet->NewRowIterator(schema.CopyWithoutColumnIds(),
                                                   boost::none /* transaction_id */,
                                                   {} /* read_hybrid_time */,
                                                   kPgTablespaceTableId));
  QLTableRow source_row;
  ColumnId oid_col_id = VERIFY_RESULT(schema.ColumnIdByName("oid"));
  ColumnId options_id =
      VERIFY_RESULT(schema.ColumnIdByName("spcoptions"));

  // Loop through the pg_tablespace catalog table. Each row in this table represents
  // a tablespace. Populate 'tablespace_map' with the tablespace id and corresponding
  // placement info for each tablespace encountered in this catalog table.
  auto tablespace_map = std::make_shared<TablespaceIdToReplicationInfoMap>();
  while (VERIFY_RESULT(iter->HasNext())) {
    RETURN_NOT_OK(iter->NextRow(&source_row));
    // Fetch the oid.
    auto oid = source_row.GetValue(oid_col_id);
    if (!oid) {
      return STATUS(Corruption, "Could not read oid column from pg_tablespace");
    }

    // Get the tablespace id.
    const TablespaceId tablespace_id = GetPgsqlTablespaceId(oid->uint32_value());

    // Fetch the options specified for the tablespace.
    const auto& options = source_row.GetValue(options_id);
    if (!options) {
      return STATUS(Corruption, "Could not read spcoptions column from pg_tablespace");
    }

    VLOG(2) << "Tablespace " << tablespace_id << " -> " << options.value().DebugString();

    // If no spcoptions found, then this tablespace has no placement info
    // associated with it. Tables associated with this tablespace will not
    // have any custom placement policy.
    if (options->binary_value().empty()) {
      // Storing boost::none lets the client know that the tables associated with
      // this tablespace will not have any custom placement policy for them.
      const auto& ret = tablespace_map->emplace(tablespace_id, boost::none);
      // This map should not have already had an element associated with this
      // tablespace.
      DCHECK(ret.second);
      continue;
    }

    // Parse the reloptions array associated with this tablespace and construct
    // the ReplicationInfoPB. The ql_value is just the raw value read from the pg_tablespace
    // catalog table. This was stored in postgres as a text array, but processed by DocDB as
    // a binary value. So first process this binary value and convert it to text array of options.
    vector<QLValuePB> placement_options;
    RETURN_NOT_OK(yb::docdb::ExtractTextArrayFromQLBinaryValue(
          options.value(), &placement_options));

    // Fetch the status and print the tablespace option along with the status.
    const auto& replication_info = VERIFY_RESULT(ParseReplicationInfo(
          tablespace_id, placement_options));
    const auto& ret = tablespace_map->emplace(tablespace_id, replication_info);
    // This map should not have already had an element associated with this
    // tablespace.
    DCHECK(ret.second);
  }

  return tablespace_map;
}

Status SysCatalogTable::ReadPgClassInfo(
    const uint32_t database_oid,
    TableToTablespaceIdMap *const table_to_tablespace_map) {

  TRACE_EVENT0("master", "ReadPgClass");

  if (!table_to_tablespace_map) {
    return STATUS(InternalError, "table_to_tablespace_map not initialized");
  }

  const tablet::TabletPtr tablet = tablet_peer()->shared_tablet();

  const auto& pg_table_id = GetPgsqlTableId(database_oid, kPgClassTableOid);
  const auto& table_info = VERIFY_RESULT(
      tablet->metadata()->GetTableInfo(pg_table_id));
  const Schema& schema = table_info->schema;

  Schema projection;
  RETURN_NOT_OK(schema.CreateProjectionByNames({"oid", "reltablespace", "relkind"}, &projection,
                schema.num_key_columns()));
  const auto oid_col_id = VERIFY_RESULT(projection.ColumnIdByName("oid")).rep();
  const auto relkind_col_id = VERIFY_RESULT(projection.ColumnIdByName("relkind")).rep();
  const auto tablespace_col_id = VERIFY_RESULT(projection.ColumnIdByName("reltablespace")).rep();
  auto iter = VERIFY_RESULT(tablet->NewRowIterator(
    projection.CopyWithoutColumnIds(), boost::none /* transaction_id */,
    {} /* read_hybrid_time */, pg_table_id));

  QLTableRow row;
  // Pg_class table contains a row for every database object (tables/indexes/
  // composite types etc). Each such row contains a lot of information about the
  // database object itself. But here, we are trying to fetch table->tablespace
  // information. We iterate through every row in the catalog table and try to build
  // the table/index->placement info.
  while (VERIFY_RESULT(iter->HasNext())) {
    RETURN_NOT_OK(iter->NextRow(&row));

    // First process the oid of the object corresponding to the current row.
    const auto& oid_col = row.GetValue(oid_col_id);
    if (!oid_col) {
      return STATUS(Corruption, "Could not read oid column from pg_class");
    }

    const uint32_t oid = oid_col->uint32_value();
    if (oid < kPgFirstNormalObjectId) {
      // This is some system level object. We are interested only in user created
      // tables and indexes whose oids are always higher than kPgFirstNormalObjectId.
      // Skip this row and move onto the next row in pg_class table.
      continue;
    }

    // Now look at the relkind of the database object.
    const auto& relkind_col = row.GetValue(relkind_col_id);
    if (!relkind_col) {
      return STATUS(Corruption, "Could not read relkind column from pg_class for oid " +
          std::to_string(oid));
    }

    const char relkind = relkind_col->int8_value();
    // From PostgreSQL docs: r = ordinary table, i = index, S = sequence, t = TOAST table,
    // v = view, m = materialized view, c = composite type, f = foreign table,
    // p = partitioned table, I = partitioned index
    if (relkind != 'r' && relkind != 'i' && relkind != 'p' && relkind != 'I') {
      // This database object is not a table/index/partitioned table/partitioned index.
      // Skip this.
      continue;
    }

    // Now process the tablespace oid for this database object.
    const auto& tablespace_oid_col = row.GetValue(tablespace_col_id);
    if (!tablespace_oid_col) {
      return STATUS(Corruption, "Could not read tablespace column from pg_class");
    }

    const uint32 tablespace_oid = tablespace_oid_col->uint32_value();
    VLOG(5) << "Table oid: " << oid << " Tablespace oid: " << tablespace_oid;

    boost::optional<TablespaceId> tablespace_id = boost::none;
    // If the tablespace oid is kInvalidOid then it means this table was created
    // without a custom tablespace and its properties just default to cluster level
    // policies.
    if (tablespace_oid != kInvalidOid) {
      tablespace_id = GetPgsqlTablespaceId(tablespace_oid);
    }
    const auto& ret = table_to_tablespace_map->emplace(
        GetPgsqlTableId(database_oid, oid), tablespace_id);
    // The map should not have a duplicate entry with the same oid.
    DCHECK(ret.second);
  }
  return Status::OK();

}

Result<boost::optional<ReplicationInfoPB>> SysCatalogTable::ParseReplicationInfo(
  const TablespaceId& tablespace_id,
  const vector<QLValuePB>& options) {

  // Today only one option is supported, so this array should have only one option.
  if (options.size() != 1) {
    return STATUS(Corruption, "Unexpected number of options: " + std::to_string(options.size()) +
        " for tablespace with ID:" + tablespace_id);
  }

  const string& option = options[0].string_value();
  // The only option supported today is "replica_placement" that allows specification
  // of placement policies encoded as a JSON array. Example value:
  // replica_placement=
  //   '{"num_replicas":3, "placement_blocks": [
  //         {"cloud":"c1", "region":"r1", "zone":"z1", "min_num_replicas":1},
  //         {"cloud":"c2", "region":"r2", "zone":"z2", "min_num_replicas":1},
  //         {"cloud":"c3", "region":"r3", "zone":"z3", "min_num_replicas":1}]}'
  if (option.find("replica_placement") == string::npos) {
    return STATUS(Corruption, "Invalid option found in spcoptions for tablespace with ID:" +
        tablespace_id);
  }

  // First split the string and get only the json value in a string.
  vector<string> split;
  split = strings::Split(option, "replica_placement=", strings::SkipEmpty());
  if (split.size() != 1) {
    return STATUS(Corruption, "replica_placement option illformed: " + option +
        " for tablespace with ID:" + tablespace_id);
  }

  const string& placement_info = split[0];
  rapidjson::Document document;
  if (document.Parse(placement_info.c_str()).HasParseError() || !document.IsObject()) {
    return STATUS(Corruption, "Json parsing of replica placement option failed: " +
          placement_info + " for tablespace with ID:" + tablespace_id);
  }

  if (!document.HasMember("num_replicas") || !document["num_replicas"].IsInt()) {
    return STATUS(Corruption, "Invalid value found for \"num_replicas\" field in the placement "
            "policy for tablespace with ID:" + tablespace_id + " Placement policy: " +
            placement_info);
  }
  const int num_replicas = document["num_replicas"].GetInt();

  // Parse the placement blocks.
  if (!document.HasMember("placement_blocks") || !document["placement_blocks"].IsArray()) {
    return STATUS(Corruption, "\"placement_blocks\" field not found in the placement "
              "policy for tablespace with ID:" + tablespace_id + " Placement policy: " +
              placement_info);
  }

  const rapidjson::Value& pb = document["placement_blocks"];
  if (pb.Size() < 1) {
    return STATUS(Corruption, "\"placement_blocks\" field has empty value in the placement "
                "policy for tablespace with ID:" + tablespace_id + " Placement policy: " +
                placement_info);
  }

  ReplicationInfoPB replication_info_pb;
  master::PlacementInfoPB* live_replicas = replication_info_pb.mutable_live_replicas();
  int64 total_min_replicas = 0;
  for (int64 ii = 0; ii < pb.Size(); ++ii) {
    const rapidjson::Value& placement = pb[ii];
    auto pb = live_replicas->add_placement_blocks();
    if (!placement.HasMember("cloud") || !placement.HasMember("region") ||
        !placement.HasMember("zone") || !placement.HasMember("min_num_replicas")) {
      return STATUS(Corruption, "Missing keys in replica placement option: " +
          placement_info + " for tablespace with ID:" + tablespace_id);
    }
    if (!placement["cloud"].IsString() || !placement["region"].IsString() ||
        !placement["zone"].IsString() || !placement["min_num_replicas"].IsInt()) {
      return STATUS(Corruption, "Invalid value for replica_placement option: " +
          placement_info + " for tablespace with ID:" + tablespace_id);
    }
    pb->mutable_cloud_info()->set_placement_cloud(placement["cloud"].GetString());
    pb->mutable_cloud_info()->set_placement_region(placement["region"].GetString());
    pb->mutable_cloud_info()->set_placement_zone(placement["zone"].GetString());
    const int min_rf = placement["min_num_replicas"].GetInt();
    pb->set_min_num_replicas(min_rf);
    total_min_replicas += min_rf;
  }
  if (total_min_replicas > num_replicas) {
    return STATUS(Corruption, "Sum of min_num_replicas fields exceeds the total replication factor "
                  "in the placement policy for tablespace with ID:" + tablespace_id +
                  " Placement policy: " + placement_info);
  }
  live_replicas->set_num_replicas(num_replicas);
  return replication_info_pb;
}

Status SysCatalogTable::CopyPgsqlTables(
    const vector<TableId>& source_table_ids, const vector<TableId>& target_table_ids,
    const int64_t leader_term) {
  TRACE_EVENT0("master", "CopyPgsqlTables");

  std::unique_ptr<SysCatalogWriter> writer = NewWriter(leader_term);

  RSTATUS_DCHECK_EQ(
      source_table_ids.size(), target_table_ids.size(), InvalidArgument,
      "size mismatch between source tables and target tables");

  int batch_count = 0, total_count = 0, total_bytes = 0;
  const tablet::TabletPtr tablet = tablet_peer()->shared_tablet();
  const auto* meta = tablet->metadata();
  for (int i = 0; i < source_table_ids.size(); ++i) {
    auto& source_table_id = source_table_ids[i];
    auto& target_table_id = target_table_ids[i];

    const std::shared_ptr<tablet::TableInfo> source_table_info =
        VERIFY_RESULT(meta->GetTableInfo(source_table_id));
    const std::shared_ptr<tablet::TableInfo> target_table_info =
        VERIFY_RESULT(meta->GetTableInfo(target_table_id));
    const Schema source_projection = source_table_info->schema.CopyWithoutColumnIds();
    std::unique_ptr<common::YQLRowwiseIteratorIf> iter = VERIFY_RESULT(
        tablet->NewRowIterator(source_projection, boost::none, {}, source_table_id));
    QLTableRow source_row;

    while (VERIFY_RESULT(iter->HasNext())) {
      RETURN_NOT_OK(iter->NextRow(&source_row));

      RETURN_NOT_OK(writer->InsertPgsqlTableRow(
          source_table_info->schema, source_row, target_table_id, target_table_info->schema,
          target_table_info->schema_version, true /* is_upsert */));

      ++total_count;
      if (FLAGS_copy_tables_batch_bytes > 0 && 0 == (total_count % 128)) {
          // Break up the write into batches of roughly the same serialized size
          // in order to avoid uncontrolled large network writes.
          // ByteSizeLong is an expensive calculation so do not perform it each time

        size_t batch_bytes = writer->req().ByteSizeLong();
        if (batch_bytes > FLAGS_copy_tables_batch_bytes) {
          RETURN_NOT_OK(SyncWrite(writer.get()));

          total_bytes += batch_bytes;
          ++batch_count;
          LOG(INFO) << Format(
              "CopyPgsqlTables: Batch# $0 copied $1 rows with $2 bytes", batch_count,
              writer->req().pgsql_write_batch_size(), HumanizeBytes(batch_bytes));

          writer = NewWriter(leader_term);
        }
      }
    }
  }

  if (writer->req().pgsql_write_batch_size() > 0) {
    RETURN_NOT_OK(SyncWrite(writer.get()));
    size_t batch_bytes = writer->req().ByteSizeLong();
    total_bytes += batch_bytes;
    ++batch_count;
    LOG(INFO) << Format(
        "CopyPgsqlTables: Batch# $0 copied $1 rows with $2 bytes", batch_count,
        writer->req().pgsql_write_batch_size(), HumanizeBytes(batch_bytes));
  }

  LOG(INFO) << Format(
      "CopyPgsqlTables: Copied total $0 rows, total $1 bytes in $2 batches", total_count,
      HumanizeBytes(total_bytes), batch_count);
  return Status::OK();
}

Status SysCatalogTable::DeleteYsqlSystemTable(const string& table_id) {
  tablet_peer()->tablet_metadata()->RemoveTable(table_id);
  return Status::OK();
}

const Schema& SysCatalogTable::schema() {
  return schema_;
}

} // namespace master
} // namespace yb
