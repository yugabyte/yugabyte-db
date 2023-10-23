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

#include <glog/logging.h>

#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus_round.h"
#include "yb/consensus/log.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/tserver_error.h"

#include "yb/util/async_util.h"
#include "yb/util/logging.h"
#include "yb/util/status_format.h"
#include "yb/util/trace.h"

namespace yb {
namespace tablet {

using google::protobuf::RepeatedPtrField;
using tserver::TabletServerErrorPB;

template <>
void RequestTraits<ChangeMetadataRequestPB>::SetAllocatedRequest(
    consensus::ReplicateMsg* replicate, ChangeMetadataRequestPB* request) {
  replicate->set_allocated_change_metadata_request(request);
}

template <>
ChangeMetadataRequestPB* RequestTraits<ChangeMetadataRequestPB>::MutableRequest(
    consensus::ReplicateMsg* replicate) {
  return replicate->mutable_change_metadata_request();
}

ChangeMetadataOperation::ChangeMetadataOperation(
    Tablet* tablet, log::Log* log, const ChangeMetadataRequestPB* request)
    : ExclusiveSchemaOperation(tablet, request), log_(log) {
}

ChangeMetadataOperation::ChangeMetadataOperation(const ChangeMetadataRequestPB* request)
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

Status ChangeMetadataOperation::Prepare() {
  TRACE("PREPARE CHANGE-METADATA: Starting");

  // Decode schema
  auto has_schema = request()->has_schema();
  if (has_schema) {
    schema_holder_ = std::make_unique<Schema>();
    Status s = SchemaFromPB(request()->schema(), schema_holder_.get());
    if (!s.ok()) {
      return s.CloneAndAddErrorCode(
          tserver::TabletServerError(TabletServerErrorPB::INVALID_SCHEMA));
    }
  }

  Tablet* tablet = this->tablet();
  RETURN_NOT_OK(tablet->CreatePreparedChangeMetadata(this, schema_holder_.get()));

  SetIndexes(request()->indexes());

  TRACE("PREPARE CHANGE-METADATA: finished");
  return Status::OK();
}

Status ChangeMetadataOperation::DoReplicated(int64_t leader_term, Status* complete_status) {
  TRACE("APPLY CHANGE-METADATA: Starting");

  Tablet* tablet = this->tablet();
  log::Log* log = mutable_log();
  size_t num_operations = 0;

  if (request()->has_wal_retention_secs()) {
    // We don't consider wal retention changes as another operation because this value is always
    // sent together with the schema, as long as it has been changed in the master's sys-catalog.
    auto s = tablet->AlterWalRetentionSecs(this);
    if (s.ok()) {
      log->set_wal_retention_secs(request()->wal_retention_secs());
    } else {
      LOG(WARNING) << "T " << tablet->tablet_id() << ": Unable to alter wal retention secs";
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

  if (request()->add_multiple_tables_size()) {
    metadata_change = MetadataChange::NONE;
    if (++num_operations == 1) {
      metadata_change = MetadataChange::ADD_MULTIPLE_TABLES;
    }
  }

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
      RETURN_NOT_OK(tablet->AddTable(request()->add_table()));
      break;
    case MetadataChange::REMOVE_TABLE:
      DCHECK_EQ(1, num_operations) << "Invalid number of change metadata operations: "
                                   << num_operations;
      RETURN_NOT_OK(tablet->RemoveTable(request()->remove_table_id()));
      break;
    case MetadataChange::BACKFILL_DONE:
      DCHECK_EQ(1, num_operations) << "Invalid number of change metadata operations: "
                                   << num_operations;
      RETURN_NOT_OK(tablet->MarkBackfillDone(request()->backfill_done_table_id()));
      break;
    case MetadataChange::ADD_MULTIPLE_TABLES:
      DCHECK_EQ(1, num_operations) << "Invalid number of change metadata operations: "
                                   << num_operations;
      RETURN_NOT_OK(tablet->AddMultipleTables(request()->add_multiple_tables()));
      break;
  }

  // Now that all of the changes have been applied and the commit is durable
  // make the changes visible to readers.
  TRACE("AlterSchemaCommitCallback: making alter schema visible");
  return Status::OK();
}

Status ChangeMetadataOperation::DoAborted(const Status& status) {
  TRACE("AlterSchemaCommitCallback: transaction aborted");
  return status;
}

Status SyncReplicateChangeMetadataOperation(
    const ChangeMetadataRequestPB* req,
    TabletPeer* tablet_peer,
    int64_t term) {
  auto shared_tablet = VERIFY_RESULT(tablet_peer->shared_tablet_safe());
  auto operation = std::make_unique<ChangeMetadataOperation>(
      shared_tablet.get(), tablet_peer->log(), req);

  Synchronizer synchronizer;

  operation->set_completion_callback(synchronizer.AsStdStatusCallback());

  tablet_peer->Submit(std::move(operation), term);

  return synchronizer.Wait();
}

}  // namespace tablet
}  // namespace yb
