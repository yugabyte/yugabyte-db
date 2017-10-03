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

#include "kudu/master/sys_catalog.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "kudu/common/partial_row.h"
#include "kudu/common/partition.h"
#include "kudu/common/row_operations.h"
#include "kudu/common/schema.h"
#include "kudu/common/wire_protocol.h"
#include "kudu/consensus/consensus_meta.h"
#include "kudu/consensus/consensus_peers.h"
#include "kudu/consensus/opid_util.h"
#include "kudu/consensus/quorum_util.h"
#include "kudu/fs/fs_manager.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/master/catalog_manager.h"
#include "kudu/master/master.h"
#include "kudu/master/master.pb.h"
#include "kudu/rpc/rpc_context.h"
#include "kudu/tablet/tablet_bootstrap.h"
#include "kudu/tablet/tablet.h"
#include "kudu/tablet/transactions/write_transaction.h"
#include "kudu/tserver/tserver.pb.h"
#include "kudu/util/debug/trace_event.h"
#include "kudu/util/logging.h"
#include "kudu/util/pb_util.h"
#include "kudu/util/threadpool.h"

using kudu::consensus::CONSENSUS_CONFIG_COMMITTED;
using kudu::consensus::ConsensusMetadata;
using kudu::consensus::RaftConfigPB;
using kudu::consensus::RaftPeerPB;
using kudu::log::Log;
using kudu::log::LogAnchorRegistry;
using kudu::tablet::LatchTransactionCompletionCallback;
using kudu::tablet::Tablet;
using kudu::tablet::TabletPeer;
using kudu::tserver::WriteRequestPB;
using kudu::tserver::WriteResponsePB;
using std::shared_ptr;
using strings::Substitute;

namespace kudu {
namespace master {

static const char* const kSysCatalogTabletId = "00000000000000000000000000000000";

static const char* const kSysCatalogTableColType = "entry_type";
static const char* const kSysCatalogTableColId = "entry_id";
static const char* const kSysCatalogTableColMetadata = "metadata";

SysCatalogTable::SysCatalogTable(Master* master, MetricRegistry* metrics,
                                 ElectedLeaderCallback leader_cb)
    : metric_registry_(metrics),
      master_(master),
      leader_cb_(std::move(leader_cb)),
      old_role_(RaftPeerPB::FOLLOWER) {
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

Status SysCatalogTable::Load(FsManager *fs_manager) {
  // Load Metadata Information from disk
  scoped_refptr<tablet::TabletMetadata> metadata;
  RETURN_NOT_OK(tablet::TabletMetadata::Load(fs_manager, kSysCatalogTabletId, &metadata));

  // Verify that the schema is the current one
  if (!metadata->schema().Equals(BuildTableSchema())) {
    // TODO: In this case we probably should execute the migration step.
    return(Status::Corruption("Unexpected schema", metadata->schema().ToString()));
  }

  // Allow for statically and explicitly assigning the consensus configuration and roles through
  // the master configuration on startup.
  //
  // TODO: The following assumptions need revisiting:
  // 1. We always believe the local config options for who is in the consensus configuration.
  // 2. We always want to look up all node's UUIDs on start (via RPC).
  //    - TODO: Cache UUIDs. See KUDU-526.
  if (master_->opts().IsDistributed()) {
    LOG(INFO) << "Configuring consensus for distributed operation...";

    string tablet_id = metadata->tablet_id();
    gscoped_ptr<ConsensusMetadata> cmeta;
    RETURN_NOT_OK_PREPEND(ConsensusMetadata::Load(fs_manager, tablet_id,
                                                  fs_manager->uuid(), &cmeta),
                          "Unable to load consensus metadata for tablet " + tablet_id);

    RaftConfigPB config;
    RETURN_NOT_OK(SetupDistributedConfig(master_->opts(), &config));
    cmeta->set_committed_config(config);
    RETURN_NOT_OK_PREPEND(cmeta->Flush(),
                          "Unable to persist consensus metadata for tablet " + tablet_id);
  }

  RETURN_NOT_OK(SetupTablet(metadata));
  return Status::OK();
}

Status SysCatalogTable::CreateNew(FsManager *fs_manager) {
  // Create the new Metadata
  scoped_refptr<tablet::TabletMetadata> metadata;
  Schema schema = BuildTableSchema();
  PartitionSchema partition_schema;
  RETURN_NOT_OK(PartitionSchema::FromPB(PartitionSchemaPB(), schema, &partition_schema));

  vector<KuduPartialRow> split_rows;
  vector<Partition> partitions;
  RETURN_NOT_OK(partition_schema.CreatePartitions(split_rows, schema, &partitions));
  DCHECK_EQ(1, partitions.size());

  RETURN_NOT_OK(tablet::TabletMetadata::CreateNew(fs_manager,
                                                  kSysCatalogTabletId,
                                                  table_name(),
                                                  schema, partition_schema,
                                                  partitions[0],
                                                  tablet::TABLET_DATA_READY,
                                                  &metadata));

  RaftConfigPB config;
  if (master_->opts().IsDistributed()) {
    RETURN_NOT_OK_PREPEND(SetupDistributedConfig(master_->opts(), &config),
                          "Failed to initialize distributed config");
  } else {
    config.set_local(true);
    config.set_opid_index(consensus::kInvalidOpIdIndex);
    RaftPeerPB* peer = config.add_peers();
    peer->set_permanent_uuid(fs_manager->uuid());
    peer->set_member_type(RaftPeerPB::VOTER);
  }

  string tablet_id = metadata->tablet_id();
  gscoped_ptr<ConsensusMetadata> cmeta;
  RETURN_NOT_OK_PREPEND(ConsensusMetadata::Create(fs_manager, tablet_id, fs_manager->uuid(),
                                                  config, consensus::kMinimumTerm, &cmeta),
                        "Unable to persist consensus metadata for tablet " + tablet_id);

  return SetupTablet(metadata);
}

Status SysCatalogTable::SetupDistributedConfig(const MasterOptions& options,
                                               RaftConfigPB* committed_config) {
  DCHECK(options.IsDistributed());

  RaftConfigPB new_config;
  new_config.set_local(false);
  new_config.set_opid_index(consensus::kInvalidOpIdIndex);

  // Build the set of followers from our server options.
  for (const HostPort& host_port : options.master_addresses) {
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
      RETURN_NOT_OK_PREPEND(consensus::SetPermanentUuidForRemotePeer(master_->messenger(),
                                                                     &new_peer),
                            Substitute("Unable to resolve UUID for peer $0",
                                       peer.ShortDebugString()));
      resolved_config.add_peers()->CopyFrom(new_peer);
    }
  }

  RETURN_NOT_OK(consensus::VerifyRaftConfig(resolved_config, consensus::COMMITTED_QUORUM));
  VLOG(1) << "Distributed Raft configuration: " << resolved_config.ShortDebugString();

  *committed_config = resolved_config;
  return Status::OK();
}

void SysCatalogTable::SysCatalogStateChanged(const string& tablet_id, const string& reason) {
  CHECK_EQ(tablet_id, tablet_peer_->tablet_id());
  scoped_refptr<consensus::Consensus> consensus  = tablet_peer_->shared_consensus();
  if (!consensus) {
    LOG_WITH_PREFIX(WARNING) << "Received notification of tablet state change "
                             << "but tablet no longer running. Tablet ID: "
                             << tablet_id << ". Reason: " << reason;
    return;
  }
  consensus::ConsensusStatePB cstate = consensus->ConsensusState(CONSENSUS_CONFIG_COMMITTED);
  LOG_WITH_PREFIX(INFO) << "SysCatalogTable state changed. Reason: " << reason << ". "
                        << "Latest consensus state: " << cstate.ShortDebugString();
  RaftPeerPB::Role new_role = GetConsensusRole(tablet_peer_->permanent_uuid(), cstate);
  LOG_WITH_PREFIX(INFO) << "This master's current role is: "
                        << RaftPeerPB::Role_Name(new_role)
                        << ", previous role was: " << RaftPeerPB::Role_Name(old_role_);
  if (new_role == RaftPeerPB::LEADER) {
    CHECK_OK(leader_cb_.Run());
  }
}

Status SysCatalogTable::SetupTablet(const scoped_refptr<tablet::TabletMetadata>& metadata) {
  shared_ptr<Tablet> tablet;
  scoped_refptr<Log> log;

  InitLocalRaftPeerPB();

  // TODO: handle crash mid-creation of tablet? do we ever end up with a
  // partially created tablet here?
  tablet_peer_.reset(new TabletPeer(
      metadata,
      local_peer_pb_,
      apply_pool_.get(),
      Bind(&SysCatalogTable::SysCatalogStateChanged, Unretained(this), metadata->tablet_id())));

  consensus::ConsensusBootstrapInfo consensus_info;
  tablet_peer_->SetBootstrapping();
  RETURN_NOT_OK(BootstrapTablet(metadata,
                                scoped_refptr<server::Clock>(master_->clock()),
                                master_->mem_tracker(),
                                metric_registry_,
                                tablet_peer_->status_listener(),
                                &tablet,
                                &log,
                                tablet_peer_->log_anchor_registry(),
                                &consensus_info));

  // TODO: Do we have a setSplittable(false) or something from the outside is
  // handling split in the TS?

  RETURN_NOT_OK_PREPEND(tablet_peer_->Init(tablet,
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
  key_schema_ = schema_.CreateKeyProjection();
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

Status SysCatalogTable::SyncWrite(const WriteRequestPB *req, WriteResponsePB *resp) {
  CountDownLatch latch(1);
  gscoped_ptr<tablet::TransactionCompletionCallback> txn_callback(
    new LatchTransactionCompletionCallback<WriteResponsePB>(&latch, resp));
  auto tx_state = new tablet::WriteTransactionState(tablet_peer_.get(), req, resp);
  tx_state->set_completion_callback(txn_callback.Pass());

  RETURN_NOT_OK(tablet_peer_->SubmitWrite(tx_state));
  latch.Wait();

  if (resp->has_error()) {
    return StatusFromPB(resp->error().status());
  }
  if (resp->per_row_errors_size() > 0) {
    for (const WriteResponsePB::PerRowErrorPB& error : resp->per_row_errors()) {
      LOG(WARNING) << "row " << error.row_index() << ": " << StatusFromPB(error.error()).ToString();
    }
    return Status::Corruption("One or more rows failed to write");
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
  CHECK_OK(builder.AddKeyColumn(kSysCatalogTableColId, STRING));
  CHECK_OK(builder.AddColumn(kSysCatalogTableColMetadata, STRING));
  return builder.Build();
}

// ==================================================================
// Table related methods
// ==================================================================

Status SysCatalogTable::AddTable(const TableInfo *table) {
  TRACE_EVENT1("master", "SysCatalogTable::AddTable",
               "table", table->ToString());
  faststring metadata_buf;
  if (!pb_util::SerializeToString(table->metadata().dirty().pb, &metadata_buf)) {
    return Status::Corruption("Unable to serialize SysCatalogTablesEntryPB for tablet",
                              table->metadata().dirty().name());
  }

  WriteRequestPB req;
  WriteResponsePB resp;
  req.set_tablet_id(kSysCatalogTabletId);
  RETURN_NOT_OK(SchemaToPB(schema_, req.mutable_schema()));

  KuduPartialRow row(&schema_);
  CHECK_OK(row.SetInt8(kSysCatalogTableColType, TABLES_ENTRY));
  CHECK_OK(row.SetString(kSysCatalogTableColId, table->id()));
  CHECK_OK(row.SetString(kSysCatalogTableColMetadata, metadata_buf));
  RowOperationsPBEncoder enc(req.mutable_row_operations());
  enc.Add(RowOperationsPB::INSERT, row);

  RETURN_NOT_OK(SyncWrite(&req, &resp));
  return Status::OK();
}

Status SysCatalogTable::UpdateTable(const TableInfo *table) {
  TRACE_EVENT1("master", "SysCatalogTable::UpdateTable",
               "table", table->ToString());

  faststring metadata_buf;
  if (!pb_util::SerializeToString(table->metadata().dirty().pb, &metadata_buf)) {
    return Status::Corruption("Unable to serialize SysCatalogTablesEntryPB for tablet",
                              table->id());
  }

  WriteRequestPB req;
  WriteResponsePB resp;
  req.set_tablet_id(kSysCatalogTabletId);
  RETURN_NOT_OK(SchemaToPB(schema_, req.mutable_schema()));

  KuduPartialRow row(&schema_);
  CHECK_OK(row.SetInt8(kSysCatalogTableColType, TABLES_ENTRY));
  CHECK_OK(row.SetString(kSysCatalogTableColId, table->id()));
  CHECK_OK(row.SetString(kSysCatalogTableColMetadata, metadata_buf));
  RowOperationsPBEncoder enc(req.mutable_row_operations());
  enc.Add(RowOperationsPB::UPDATE, row);

  RETURN_NOT_OK(SyncWrite(&req, &resp));
  return Status::OK();
}

Status SysCatalogTable::DeleteTable(const TableInfo *table) {
  TRACE_EVENT1("master", "SysCatalogTable::DeleteTable",
               "table", table->ToString());
  WriteRequestPB req;
  WriteResponsePB resp;
  req.set_tablet_id(kSysCatalogTableColMetadata);
  RETURN_NOT_OK(SchemaToPB(schema_, req.mutable_schema()));

  KuduPartialRow row(&schema_);
  CHECK_OK(row.SetInt8(kSysCatalogTableColType, TABLES_ENTRY));
  CHECK_OK(row.SetString(kSysCatalogTableColId, table->id()));

  RowOperationsPBEncoder enc(req.mutable_row_operations());
  enc.Add(RowOperationsPB::DELETE, row);

  RETURN_NOT_OK(SyncWrite(&req, &resp));
  return Status::OK();
}

Status SysCatalogTable::VisitTables(TableVisitor* visitor) {
  TRACE_EVENT0("master", "SysCatalogTable::VisitTables");

  const int8_t tables_entry = TABLES_ENTRY;
  const int type_col_idx = schema_.find_column(kSysCatalogTableColType);
  CHECK(type_col_idx != Schema::kColumnNotFound);

  ColumnRangePredicate pred_tables(schema_.column(type_col_idx),
                                   &tables_entry, &tables_entry);
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

      RETURN_NOT_OK(VisitTableFromRow(block.row(i), visitor));
    }
  }
  return Status::OK();
}

Status SysCatalogTable::VisitTableFromRow(const RowBlockRow& row,
                                          TableVisitor* visitor) {
  const Slice* table_id =
      schema_.ExtractColumnFromRow<STRING>(row, schema_.find_column(kSysCatalogTableColId));
  const Slice* data =
      schema_.ExtractColumnFromRow<STRING>(row, schema_.find_column(kSysCatalogTableColMetadata));

  SysTablesEntryPB metadata;
  RETURN_NOT_OK_PREPEND(pb_util::ParseFromArray(&metadata, data->data(), data->size()),
                        "Unable to parse metadata field for table " + table_id->ToString());

  RETURN_NOT_OK(visitor->VisitTable(table_id->ToString(), metadata));
  return Status::OK();
}

// ==================================================================
// Tablet related methods
// ==================================================================

Status SysCatalogTable::AddTabletsToPB(const vector<TabletInfo*>& tablets,
                                       RowOperationsPB::Type op_type,
                                       RowOperationsPB* ops) const {
  faststring metadata_buf;
  KuduPartialRow row(&schema_);
  RowOperationsPBEncoder enc(ops);
  for (const TabletInfo *tablet : tablets) {
    if (!pb_util::SerializeToString(tablet->metadata().dirty().pb, &metadata_buf)) {
      return Status::Corruption("Unable to serialize SysCatalogTabletsEntryPB for tablet",
                                tablet->tablet_id());
    }

    CHECK_OK(row.SetInt8(kSysCatalogTableColType, TABLETS_ENTRY));
    CHECK_OK(row.SetString(kSysCatalogTableColId, tablet->tablet_id()));
    CHECK_OK(row.SetString(kSysCatalogTableColMetadata, metadata_buf));
    enc.Add(op_type, row);
  }
  return Status::OK();
}

Status SysCatalogTable::AddAndUpdateTablets(const vector<TabletInfo*>& tablets_to_add,
                                            const vector<TabletInfo*>& tablets_to_update) {
  TRACE_EVENT2("master", "AddAndUpdateTablets",
               "num_add", tablets_to_add.size(),
               "num_update", tablets_to_update.size());

  WriteRequestPB req;
  WriteResponsePB resp;
  req.set_tablet_id(kSysCatalogTabletId);
  RETURN_NOT_OK(SchemaToPB(schema_, req.mutable_schema()));

  // Insert new Tablets
  if (!tablets_to_add.empty()) {
    RETURN_NOT_OK(AddTabletsToPB(tablets_to_add, RowOperationsPB::INSERT,
                                 req.mutable_row_operations()));
  }

  // Update already existing Tablets
  if (!tablets_to_update.empty()) {
    RETURN_NOT_OK(AddTabletsToPB(tablets_to_update, RowOperationsPB::UPDATE,
                                 req.mutable_row_operations()));
  }

  RETURN_NOT_OK(SyncWrite(&req, &resp));
  return Status::OK();
}

Status SysCatalogTable::AddTablets(const vector<TabletInfo*>& tablets) {
  vector<TabletInfo*> empty_tablets;
  return AddAndUpdateTablets(tablets, empty_tablets);
}

Status SysCatalogTable::UpdateTablets(const vector<TabletInfo*>& tablets) {
  vector<TabletInfo*> empty_tablets;
  return AddAndUpdateTablets(empty_tablets, tablets);
}

Status SysCatalogTable::DeleteTablets(const vector<TabletInfo*>& tablets) {
  TRACE_EVENT1("master", "DeleteTablets",
               "num_tablets", tablets.size());
  WriteRequestPB req;
  WriteResponsePB resp;
  req.set_tablet_id(kSysCatalogTabletId);
  RETURN_NOT_OK(SchemaToPB(schema_, req.mutable_schema()));

  RowOperationsPBEncoder enc(req.mutable_row_operations());
  KuduPartialRow row(&schema_);
  for (const TabletInfo* tablet : tablets) {
    CHECK_OK(row.SetInt8(kSysCatalogTableColType, TABLETS_ENTRY));
    CHECK_OK(row.SetString(kSysCatalogTableColId, tablet->tablet_id()));
    enc.Add(RowOperationsPB::DELETE, row);
  }

  RETURN_NOT_OK(SyncWrite(&req, &resp));
  return Status::OK();
}

Status SysCatalogTable::VisitTabletFromRow(const RowBlockRow& row, TabletVisitor *visitor) {
  const Slice *tablet_id =
    schema_.ExtractColumnFromRow<STRING>(row, schema_.find_column(kSysCatalogTableColId));
  const Slice *data =
    schema_.ExtractColumnFromRow<STRING>(row, schema_.find_column(kSysCatalogTableColMetadata));

  SysTabletsEntryPB metadata;
  RETURN_NOT_OK_PREPEND(pb_util::ParseFromArray(&metadata, data->data(), data->size()),
                        "Unable to parse metadata field for tablet " + tablet_id->ToString());

  // Upgrade from the deprecated start/end-key fields to the 'partition' field.
  if (!metadata.has_partition()) {
    metadata.mutable_partition()->set_partition_key_start(
        metadata.deprecated_start_key());
    metadata.mutable_partition()->set_partition_key_end(
        metadata.deprecated_end_key());
    metadata.clear_deprecated_start_key();
    metadata.clear_deprecated_end_key();
  }

  RETURN_NOT_OK(visitor->VisitTablet(metadata.table_id(), tablet_id->ToString(), metadata));
  return Status::OK();
}

Status SysCatalogTable::VisitTablets(TabletVisitor* visitor) {
  TRACE_EVENT0("master", "SysCatalogTable::VisitTablets");
  const int8_t tablets_entry = TABLETS_ENTRY;
  const int type_col_idx = schema_.find_column(kSysCatalogTableColType);
  CHECK(type_col_idx != Schema::kColumnNotFound);

  ColumnRangePredicate pred_tablets(schema_.column(type_col_idx),
                                   &tablets_entry, &tablets_entry);
  ScanSpec spec;
  spec.AddPredicate(pred_tablets);

  gscoped_ptr<RowwiseIterator> iter;
  RETURN_NOT_OK(tablet_peer_->tablet()->NewRowIterator(schema_, &iter));
  RETURN_NOT_OK(iter->Init(&spec));

  Arena arena(32 * 1024, 256 * 1024);
  RowBlock block(iter->schema(), 512, &arena);
  while (iter->HasNext()) {
    RETURN_NOT_OK(iter->NextBlock(&block));
    for (size_t i = 0; i < block.nrows(); i++) {
      if (!block.selection_vector()->IsRowSelected(i)) continue;

      RETURN_NOT_OK(VisitTabletFromRow(block.row(i), visitor));
    }
  }
  return Status::OK();
}

void SysCatalogTable::InitLocalRaftPeerPB() {
  local_peer_pb_.set_permanent_uuid(master_->fs_manager()->uuid());
  Sockaddr addr = master_->first_rpc_address();
  HostPort hp;
  CHECK_OK(HostPortFromSockaddrReplaceWildcard(addr, &hp));
  CHECK_OK(HostPortToPB(hp, local_peer_pb_.mutable_last_known_addr()));
}

} // namespace master
} // namespace kudu
