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

#include "yb/tablet/operations/change_metadata_operation.h"

#include "yb/util/logging.h"

#include "yb/common/schema_pbutil.h"
#include "yb/common/schema.h"

#include "yb/consensus/consensus.messages.h"
#include "yb/consensus/consensus_round.h"
#include "yb/consensus/log.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/tserver_error.h"

#include "yb/util/async_util.h"
#include "yb/util/debug-util.h"
#include "yb/util/flags/flag_tags.h"
#include "yb/util/logging.h"
#include "yb/util/status_format.h"
#include "yb/util/trace.h"

DEFINE_test_flag(bool, ignore_apply_change_metadata_on_followers, false,
                 "Used in tests to ignore applying change metadata operation"
                 " on followers.");

DECLARE_bool(TEST_invalidate_last_change_metadata_op);

namespace yb {
namespace tablet {

using google::protobuf::RepeatedPtrField;
using tserver::TabletServerErrorPB;

template <>
void RequestTraits<LWChangeMetadataRequestPB>::SetAllocatedRequest(
    consensus::LWReplicateMsg* replicate, LWChangeMetadataRequestPB* request) {
  replicate->ref_change_metadata_request(request);
}

template <>
LWChangeMetadataRequestPB* RequestTraits<LWChangeMetadataRequestPB>::MutableRequest(
    consensus::LWReplicateMsg* replicate) {
  return replicate->mutable_change_metadata_request();
}

ChangeMetadataOperation::ChangeMetadataOperation(
    TabletPtr tablet, log::Log* log, const LWChangeMetadataRequestPB* request)
    : ExclusiveSchemaOperation(std::move(tablet), request), log_(log) {
}

ChangeMetadataOperation::ChangeMetadataOperation(const LWChangeMetadataRequestPB* request)
    : ChangeMetadataOperation(nullptr, nullptr, request) {
}

ChangeMetadataOperation::~ChangeMetadataOperation() = default;

void ChangeMetadataOperation::SetIndexes(const RepeatedPtrField<IndexInfoPB>& indexes) {
  index_map_.FromPB(indexes);
}

std::string ChangeMetadataOperation::ToString() const {
  return Format("ChangeMetadataOperation { hybrid_time: $0 schema: $1 request: $2 }",
                hybrid_time_even_if_unset(), schema_, request());
}

Status ChangeMetadataOperation::Prepare(IsLeaderSide is_leader_side) {
  TRACE("PREPARE CHANGE-METADATA: Starting");

  // Decode schema
  auto has_schema = request()->has_schema();
  if (has_schema) {
    schema_holder_ = std::make_unique<Schema>();
    Status s = SchemaFromPB(request()->schema().ToGoogleProtobuf(), schema_holder_.get());
    if (!s.ok()) {
      return s.CloneAndAddErrorCode(
          tserver::TabletServerError(TabletServerErrorPB::INVALID_SCHEMA));
    }
  }

  TabletPtr tablet = VERIFY_RESULT(tablet_safe());
  RETURN_NOT_OK(tablet->CreatePreparedChangeMetadata(
      this, schema_holder_.get(), is_leader_side));

  SetIndexes(ToRepeatedPtrField(request()->indexes()));

  TRACE("PREPARE CHANGE-METADATA: finished");
  return Status::OK();
}

Status ChangeMetadataOperation::Apply(int64_t leader_term, Status* complete_status) {
  if (PREDICT_FALSE(FLAGS_TEST_ignore_apply_change_metadata_on_followers)) {
    LOG_WITH_PREFIX(INFO) << "Ignoring apply of change metadata ops on followers";
    return Status::OK();
  }

  TRACE("APPLY CHANGE-METADATA: Starting");

  TabletPtr tablet = VERIFY_RESULT(tablet_safe());
  log::Log* log = mutable_log();
  size_t num_operations = 0;

  if (request()->has_wal_retention_secs()) {
    // We don't consider wal retention changes as another operation because this value is always
    // sent together with the schema, as long as it has been changed in the master's sys-catalog.
    auto s = tablet->AlterWalRetentionSecs(this);
    if (s.ok()) {
      log->set_wal_retention_secs(request()->wal_retention_secs());
    } else {
      LOG(WARNING) << "T " << tablet->tablet_id() << " Unable to alter wal retention secs: " << s;
    }
  }

  // Only perform one operation.
  enum MetadataChange {
    NONE,
    SCHEMA,
    ADD_TABLE,
    REMOVE_TABLE,
    BACKFILL_DONE,
    ADD_MULTIPLE_TABLES,
  };

  MetadataChange metadata_change = MetadataChange::NONE;
  bool request_has_newer_schema = false;
  if (request()->has_schema()) {
    metadata_change = MetadataChange::SCHEMA;
    request_has_newer_schema = tablet->metadata()->schema_version() < schema_version();
    if (request_has_newer_schema) {
      ++num_operations;
    }
  }

  if (request()->has_add_table()) {
    metadata_change = MetadataChange::NONE;
    if (++num_operations == 1) {
      metadata_change = MetadataChange::ADD_TABLE;
    }
  }

  if (request()->has_remove_table_id()) {
    metadata_change = MetadataChange::NONE;
    if (++num_operations == 1) {
      metadata_change = MetadataChange::REMOVE_TABLE;
    }
  }

  if (request()->has_mark_backfill_done()) {
    metadata_change = MetadataChange::NONE;
    if (++num_operations == 1) {
      metadata_change = MetadataChange::BACKFILL_DONE;
    }
  }

  if (!request()->add_multiple_tables().empty()) {
    metadata_change = MetadataChange::NONE;
    if (++num_operations == 1) {
      metadata_change = MetadataChange::ADD_MULTIPLE_TABLES;
    }
  }

  // Get the op id corresponding to this raft op.
  const OpId id = op_id();

  switch (metadata_change) {
    case MetadataChange::NONE:
      return STATUS_FORMAT(
          InvalidArgument, "Wrong number of operations in Change Metadata Operation: $0",
          num_operations);
    case MetadataChange::SCHEMA:
      if (!request_has_newer_schema) {
        LOG_WITH_PREFIX(INFO)
            << "Already running schema version " << tablet->metadata()->schema_version()
            << " got alter request for version " << schema_version();
        break;
      }
      DCHECK_EQ(1, num_operations) << "Invalid number of change metadata operations: "
                                   << num_operations;
      RETURN_NOT_OK(tablet->AlterSchema(this));
      log->SetSchemaForNextLogSegment(*DCHECK_NOTNULL(schema()), schema_version());
      break;
    case MetadataChange::ADD_TABLE:
      DCHECK_EQ(1, num_operations) << "Invalid number of change metadata operations: "
                                   << num_operations;
      RETURN_NOT_OK(tablet->AddTable(request()->add_table().ToGoogleProtobuf(), id));
      break;
    case MetadataChange::REMOVE_TABLE:
      DCHECK_EQ(1, num_operations) << "Invalid number of change metadata operations: "
                                   << num_operations;
      RETURN_NOT_OK(tablet->RemoveTable(request()->remove_table_id().ToBuffer(), id));
      break;
    case MetadataChange::BACKFILL_DONE:
      DCHECK_EQ(1, num_operations) << "Invalid number of change metadata operations: "
                                   << num_operations;
      RETURN_NOT_OK(tablet->MarkBackfillDone(
          id, request()->backfill_done_table_id().ToBuffer()));
      break;
    case MetadataChange::ADD_MULTIPLE_TABLES:
      DCHECK_EQ(1, num_operations) << "Invalid number of change metadata operations: "
                                   << num_operations;
      RETURN_NOT_OK(tablet->AddMultipleTables(
          ToRepeatedPtrField(request()->add_multiple_tables()), id));
      break;
  }

  // Now that all of the changes have been applied and the commit is durable
  // make the changes visible to readers.
  TRACE("AlterSchemaCommitCallback: making alter schema visible");
  return Status::OK();
}

Status ChangeMetadataOperation::DoReplicated(int64_t leader_term, Status* complete_status) {
  return Apply(leader_term, complete_status);
}

Status ChangeMetadataOperation::DoAborted(const Status& status) {
  TRACE("AlterSchemaCommitCallback: transaction aborted");
  return status;
}

Status SyncReplicateChangeMetadataOperation(
    const ChangeMetadataRequestPB* req,
    TabletPeer* tablet_peer,
    int64_t term) {
  auto operation = std::make_unique<ChangeMetadataOperation>(
      VERIFY_RESULT(tablet_peer->shared_tablet_safe()), tablet_peer->log());
  operation->AllocateRequest()->CopyFrom(*req);

  Synchronizer synchronizer;

  operation->set_completion_callback(synchronizer.AsStdStatusCallback());

  tablet_peer->Submit(std::move(operation), term);

  return synchronizer.Wait();
}

}  // namespace tablet
}  // namespace yb
