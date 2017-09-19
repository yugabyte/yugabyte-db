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

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <boost/optional.hpp>

#include "yb/common/partial_row.h"
#include "yb/common/partition.h"
#include "yb/common/row_operations.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"
#include "yb/common/ql_value.h"

#include "yb/consensus/log_anchor_registry.h"
#include "yb/consensus/consensus_meta.h"
#include "yb/consensus/consensus_peers.h"
#include "yb/consensus/opid_util.h"
#include "yb/consensus/quorum_util.h"

#include "yb/fs/fs_manager.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/master.pb.h"
#include "yb/rpc/rpc_context.h"
#include "yb/tablet/tablet_bootstrap.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_options.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tablet/transactions/write_transaction.h"
#include "yb/util/flag_tags.h"
#include "yb/util/logging.h"
#include "yb/util/threadpool.h"

using std::shared_ptr;
using std::unique_ptr;

using yb::consensus::CONSENSUS_CONFIG_ACTIVE;
using yb::consensus::CONSENSUS_CONFIG_COMMITTED;
using yb::consensus::ConsensusMetadata;
using yb::consensus::RaftConfigPB;
using yb::consensus::RaftPeerPB;
using yb::log::Log;
using yb::log::LogAnchorRegistry;
using yb::tablet::LatchTransactionCompletionCallback;
using yb::tablet::Tablet;
using yb::tablet::TabletPeer;
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

DEFINE_int32(master_discovery_timeout_ms, 3600000,
             "Timeout for masters to discover each other during cluster creation/startup");
TAG_FLAG(master_discovery_timeout_ms, hidden);


namespace yb {
namespace master {

std::string SysCatalogTable::schema_column_type() { return kSysCatalogTableColType; }

std::string SysCatalogTable::schema_column_id() { return kSysCatalogTableColId; }

std::string SysCatalogTable::schema_column_metadata() { return kSysCatalogTableColMetadata; }

SysCatalogTable::SysCatalogTable(Master* master, MetricRegistry* metrics,
                                 ElectedLeaderCallback leader_cb)
    : metric_registry_(metrics),
      master_(master),
      leader_cb_(std::move(leader_cb)) {
  CHECK_OK(ThreadPoolBuilder("apply").Build(&apply_pool_));
}

SysCatalogTable::~SysCatalogTable() {
}

void SysCatalogTable::Shutdown() {
  if (tablet_peer_) {
    tablet_peer_->Shutdown();
  }
  apply_pool_->Shutdown();
}

Status SysCatalogTable::ConvertConfigToMasterAddresses(
    const RaftConfigPB& config,
    bool check_missing_uuids) {
  std::shared_ptr<std::vector<HostPort>> loaded_master_addresses =
    std::make_shared<std::vector<HostPort>>();
  bool has_missing_uuids = false;
  for (const auto& peer : config.peers()) {
    HostPort hp;
    RETURN_NOT_OK(HostPortFromPB(peer.last_known_addr(), &hp));
    if (check_missing_uuids && !peer.has_permanent_uuid()) {
      LOG(WARNING) << "No uuid for master peer at " << hp.ToString();
      has_missing_uuids = true;
      break;
    }

    loaded_master_addresses->push_back(hp);
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
  gscoped_ptr<ConsensusMetadata> cmeta;
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
  if (master_->opts().IsClusterCreationMode()) {
    return STATUS(IllegalState, Substitute(
        "In cluster creation mode for ($0), but found local consensus metadata",
        HostPort::ToCommaSeparatedString(*master_->opts().GetMasterAddresses())));
  }
  // Load Metadata Information from disk
  scoped_refptr<tablet::TabletMetadata> metadata;
  RETURN_NOT_OK(tablet::TabletMetadata::Load(fs_manager, kSysCatalogTabletId, &metadata));

  // Verify that the schema is the current one
  if (!metadata->schema().Equals(BuildTableSchema())) {
    // TODO: In this case we probably should execute the migration step.
    return(STATUS(Corruption, "Unexpected schema", metadata->schema().ToString()));
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
  string tablet_id = metadata->tablet_id();
  gscoped_ptr<ConsensusMetadata> cmeta;
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
  if (!master_->opts().IsClusterCreationMode()) {
    return STATUS(IllegalState, "Need to create data, but am not in cluster creation mode!");
  }
  // Create the new Metadata
  scoped_refptr<tablet::TabletMetadata> metadata;
  Schema schema = BuildTableSchema();
  PartitionSchema partition_schema;
  RETURN_NOT_OK(PartitionSchema::FromPB(PartitionSchemaPB(), schema, &partition_schema));

  vector<YBPartialRow> split_rows;
  vector<Partition> partitions;
  RETURN_NOT_OK(partition_schema.CreatePartitions(split_rows, schema, &partitions));
  DCHECK_EQ(1, partitions.size());

  RETURN_NOT_OK(tablet::TabletMetadata::CreateNew(
    fs_manager,
    kSysCatalogTableId,
    kSysCatalogTabletId,
    table_name(),
    TableType::YQL_TABLE_TYPE,
    schema, partition_schema,
    partitions[0],
    tablet::TABLET_DATA_READY,
    &metadata));

  RaftConfigPB config;
  RETURN_NOT_OK_PREPEND(SetupConfig(master_->opts(), &config),
                        "Failed to initialize distributed config");

  RETURN_NOT_OK(CreateAndFlushConsensusMeta(fs_manager, config, consensus::kMinimumTerm));

  return SetupTablet(metadata);
}

Status SysCatalogTable::SetupConfig(const MasterOptions& options,
                                    RaftConfigPB* committed_config) {
  RaftConfigPB new_config;
  new_config.set_opid_index(consensus::kInvalidOpIdIndex);

  // Build the set of followers from our server options.
  auto master_addresses = options.GetMasterAddresses();  // ENG-285
  for (const HostPort& host_port : *master_addresses) {
    RaftPeerPB peer;
    HostPortPB peer_host_port_pb;
    RETURN_NOT_OK(HostPortToPB(host_port, &peer_host_port_pb));
    peer.mutable_last_known_addr()->CopyFrom(peer_host_port_pb);
    peer.set_member_type(RaftPeerPB::VOTER);
    new_config.add_peers()->CopyFrom(peer);
  }

  // Now resolve UUIDs.
  // By the time a SysCatalogTable is created and initted, the masters should be
  // starting up, so this should be fine to do.
  DCHECK(master_->messenger());
  RaftConfigPB resolved_config = new_config;
  resolved_config.clear_peers();
  for (const RaftPeerPB& peer : new_config.peers()) {
    if (peer.has_permanent_uuid()) {
      resolved_config.add_peers()->CopyFrom(peer);
    } else {
      LOG(INFO) << peer.ShortDebugString()
                << " has no permanent_uuid. Determining permanent_uuid...";
      RaftPeerPB new_peer = peer;
      // TODO: Use ConsensusMetadata to cache the results of these lookups so
      // we only require RPC access to the full consensus configuration on first startup.
      // See KUDU-526.
      RETURN_NOT_OK_PREPEND(
        consensus::SetPermanentUuidForRemotePeer(
          master_->messenger(),
          FLAGS_master_discovery_timeout_ms,
          &new_peer),
        Substitute("Unable to resolve UUID for peer $0", peer.ShortDebugString()));
      resolved_config.add_peers()->CopyFrom(new_peer);
    }
  }

  LOG(INFO) << "Setting up raft configuration: " << resolved_config.ShortDebugString();

  RETURN_NOT_OK(consensus::VerifyRaftConfig(resolved_config, consensus::COMMITTED_QUORUM));

  *committed_config = resolved_config;
  return Status::OK();
}

void SysCatalogTable::SysCatalogStateChanged(
    const string& tablet_id,
    std::shared_ptr<StateChangeContext> context) {
  CHECK_EQ(tablet_id, tablet_peer_->tablet_id());
  scoped_refptr<consensus::Consensus> consensus  = tablet_peer_->shared_consensus();
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
  RaftPeerPB::Role role = GetConsensusRole(tablet_peer_->permanent_uuid(), cstate);
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
          apply_pool_->SubmitFunc(
              std::bind(&Master::InformRemovedMaster, master_, peer.last_known_addr())),
          Substitute("Error submitting removal task for uuid=$0", context->remove_uuid));
    }
  } else {
    VLOG(2) << "Reason '" << context->ToString() << "' provided in state change context, "
            << "no action needed.";
  }
}

Status SysCatalogTable::GoIntoShellMode() {
  CHECK(tablet_peer_);
  Shutdown();

  // Remove on-disk log, cmeta and tablet superblocks.
  RETURN_NOT_OK(tserver::DeleteTabletData(tablet_peer_->tablet_metadata(),
                                          tablet::TABLET_DATA_DELETED,
                                          master_->fs_manager()->uuid(),
                                          boost::none));
  RETURN_NOT_OK(tablet_peer_->tablet_metadata()->DeleteSuperBlock());

  tablet_peer_.reset();
  apply_pool_.reset();

  return Status::OK();
}

void SysCatalogTable::SetupTabletPeer(const scoped_refptr<tablet::TabletMetadata>& metadata) {
  InitLocalRaftPeerPB();

  // TODO: handle crash mid-creation of tablet? do we ever end up with a
  // partially created tablet here?
  tablet_peer_.reset(
    new TabletPeer(metadata,
                   local_peer_pb_,
                   apply_pool_.get(),
                   Bind(&SysCatalogTable::SysCatalogStateChanged,
                        Unretained(this),
                        metadata->tablet_id())));
}

Status SysCatalogTable::SetupTablet(const scoped_refptr<tablet::TabletMetadata>& metadata) {
  SetupTabletPeer(metadata);

  RETURN_NOT_OK(OpenTablet(metadata));

  return Status::OK();
}

Status SysCatalogTable::OpenTablet(const scoped_refptr<tablet::TabletMetadata>& metadata) {
  CHECK(tablet_peer_);

  shared_ptr<Tablet> tablet;
  scoped_refptr<Log> log;
  consensus::ConsensusBootstrapInfo consensus_info;
  tablet_peer_->SetBootstrapping();
  tablet::TabletOptions tablet_options;
  tablet::BootstrapTabletData data = { metadata,
                                       scoped_refptr<server::Clock>(master_->clock()),
                                       master_->mem_tracker(),
                                       metric_registry_,
                                       tablet_peer_->status_listener(),
                                       tablet_peer_->log_anchor_registry(),
                                       tablet_options,
                                       nullptr /* transaction_coordinator_context */ };
  RETURN_NOT_OK(BootstrapTablet(data, &tablet, &log, &consensus_info));

  // TODO: Do we have a setSplittable(false) or something from the outside is
  // handling split in the TS?

  RETURN_NOT_OK_PREPEND(tablet_peer_->Init(tablet,
                                           nullptr,
                                           scoped_refptr<server::Clock>(master_->clock()),
                                           master_->messenger(),
                                           log,
                                           tablet->GetMetricEntity()),
                        "Failed to Init() TabletPeer");

  RETURN_NOT_OK_PREPEND(tablet_peer_->Start(consensus_info),
                        "Failed to Start() TabletPeer");

  tablet_peer_->RegisterMaintenanceOps(master_->maintenance_manager());

  const Schema* schema = tablet->schema();
  schema_ = SchemaBuilder(*schema).BuildWithoutIds();
  schema_with_ids_ = SchemaBuilder(*schema).Build();
  return Status::OK();
}

std::string SysCatalogTable::LogPrefix() const {
  return Substitute("T $0 P $1 [$2]: ",
                    tablet_peer_->tablet_id(),
                    tablet_peer_->permanent_uuid(),
                    table_name());
}

Status SysCatalogTable::WaitUntilRunning() {
  TRACE_EVENT0("master", "SysCatalogTable::WaitUntilRunning");
  int seconds_waited = 0;
  while (true) {
    Status status = tablet_peer_->WaitUntilConsensusRunning(MonoDelta::FromSeconds(1));
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
  RETURN_NOT_OK(SchemaToPB(writer->schema_, writer->req_.mutable_schema()));
  tserver::WriteResponsePB resp;

  CountDownLatch latch(1);
  auto txn_callback = std::make_unique<LatchTransactionCompletionCallback<WriteResponsePB>>(
      &latch, &resp);
  auto tx_state = std::make_unique<tablet::WriteTransactionState>(
      tablet_peer_.get(), &writer->req_, &resp);
  tx_state->set_completion_callback(std::move(txn_callback));

  RETURN_NOT_OK(tablet_peer_->SubmitWrite(std::move(tx_state)));
  latch.Wait();

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  if (resp.per_row_errors_size() > 0) {
    for (const WriteResponsePB::PerRowErrorPB& error : resp.per_row_errors()) {
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
  auto addr = master_->first_rpc_address();
  HostPort hp;
  CHECK_OK(HostPortFromEndpointReplaceWildcard(addr, &hp));
  CHECK_OK(HostPortToPB(hp, local_peer_pb_.mutable_last_known_addr()));
}

Status SysCatalogTable::Visit(VisitorBase* visitor) {
  TRACE_EVENT0("master", "Visitor::VisitAll");

  const int8_t tables_entry = visitor->entry_type();
  const int type_col_idx = schema_.find_column(kSysCatalogTableColType);
  CHECK(type_col_idx != Schema::kColumnNotFound);

  ColumnRangePredicate pred_tables(schema_.column(type_col_idx), &tables_entry, &tables_entry);
  ScanSpec spec;
  spec.AddPredicate(pred_tables);

  gscoped_ptr<RowwiseIterator> iter;
  RETURN_NOT_OK(tablet_peer_->tablet()->NewRowIterator(schema_, &iter));
  RETURN_NOT_OK(iter->Init(&spec));

  Arena arena(32 * 1024, 256 * 1024);
  RowBlock block(iter->schema(), 512, &arena);
  while (iter->HasNext()) {
    RETURN_NOT_OK(iter->NextBlock(&block));
    for (size_t i = 0; i < block.nrows(); i++) {
      if (!block.selection_vector()->IsRowSelected(i)) continue;

      const Slice* id = schema_.ExtractColumnFromRow<BINARY>(
          block.row(i), schema_.find_column(kSysCatalogTableColId));
      const Slice* data = schema_.ExtractColumnFromRow<BINARY>(
          block.row(i), schema_.find_column(kSysCatalogTableColMetadata));
      RETURN_NOT_OK(visitor->Visit(id, data));
    }
  }
  return Status::OK();
}

} // namespace master
} // namespace yb
