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

#include "yb/tablet/operations/alter_schema_operation.h"

#include <glog/logging.h>

#include "yb/common/wire_protocol.h"
#include "yb/rpc/rpc_context.h"
#include "yb/server/hybrid_clock.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_metrics.h"
#include "yb/tserver/tserver.pb.h"
#include "yb/util/trace.h"

namespace yb {
namespace tablet {

using std::bind;
using consensus::ReplicateMsg;
using consensus::ALTER_SCHEMA_OP;
using consensus::DriverType;
using google::protobuf::RepeatedPtrField;
using strings::Substitute;
using tserver::TabletServerErrorPB;
using tserver::AlterSchemaRequestPB;
using tserver::AlterSchemaResponsePB;

void AlterSchemaOperationState::SetIndexes(const RepeatedPtrField<IndexInfoPB>& indexes) {
  index_map_.FromPB(indexes);
}

string AlterSchemaOperationState::ToString() const {
  return Substitute("AlterSchemaOperationState "
                    "[hybrid_time=$0, schema=$1, request=$2]",
                    hybrid_time_even_if_unset().ToString(),
                    schema_ == nullptr ? "(none)" : schema_->ToString(),
                    request_ == nullptr ? "(none)" : request_->ShortDebugString());
}

void AlterSchemaOperationState::AcquireSchemaLock(rw_semaphore* l) {
  TRACE("Acquiring schema lock in exclusive mode");
  schema_lock_ = std::unique_lock<rw_semaphore>(*l);
  TRACE("Acquired schema lock");
}

void AlterSchemaOperationState::ReleaseSchemaLock() {
  CHECK(schema_lock_.owns_lock());
  schema_lock_ = std::unique_lock<rw_semaphore>();
  TRACE("Released schema lock");
}


AlterSchemaOperation::AlterSchemaOperation(std::unique_ptr<AlterSchemaOperationState> state,
                                           DriverType type)
    : Operation(std::move(state), type, OperationType::kAlterSchema) {}

consensus::ReplicateMsgPtr AlterSchemaOperation::NewReplicateMsg() {
  auto result = std::make_shared<ReplicateMsg>();
  result->set_op_type(ALTER_SCHEMA_OP);
  result->mutable_alter_schema_request()->CopyFrom(*state()->request());
  return result;
}

Status AlterSchemaOperation::Prepare() {
  TRACE("PREPARE ALTER-SCHEMA: Starting");

  // Decode schema
  gscoped_ptr<Schema> schema(new Schema);
  Status s = SchemaFromPB(state()->request()->schema(), schema.get());
  if (!s.ok()) {
    state()->completion_callback()->set_error(s, TabletServerErrorPB::INVALID_SCHEMA);
    return s;
  }

  Tablet* tablet = state()->tablet();
  RETURN_NOT_OK(tablet->CreatePreparedAlterSchema(state(), schema.get()));

  state()->AddToAutoReleasePool(schema.release());

  state()->SetIndexes(state()->request()->indexes());

  TRACE("PREPARE ALTER-SCHEMA: finished");
  return s;
}

void AlterSchemaOperation::DoStart() {
  state()->TrySetHybridTimeFromClock();
  TRACE("START. HybridTime: $0",
      server::HybridClock::GetPhysicalValueMicros(state()->hybrid_time()));
}

Status AlterSchemaOperation::Apply() {
  TRACE("APPLY ALTER-SCHEMA: Starting");

  Tablet* tablet = state()->tablet();
  RETURN_NOT_OK(tablet->AlterSchema(state()));
  state()->log()->SetSchemaForNextLogSegment(*DCHECK_NOTNULL(state()->schema()),
                                             state()->schema_version());

  return Status::OK();
}

void AlterSchemaOperation::Finish(OperationResult result) {
  if (PREDICT_FALSE(result == Operation::ABORTED)) {
    TRACE("AlterSchemaCommitCallback: transaction aborted");
    state()->Finish();
    return;
  }

  // The schema lock was acquired by Tablet::CreatePreparedAlterSchema.
  // Normally, we would release it in tablet.cc after applying the operation,
  // but currently we need to wait until after the COMMIT message is logged
  // to release this lock as a workaround for KUDU-915. See the TODO in
  // Tablet::AlterSchema().
  state()->ReleaseSchemaLock();

  DCHECK_EQ(result, Operation::COMMITTED);
  // Now that all of the changes have been applied and the commit is durable
  // make the changes visible to readers.
  TRACE("AlterSchemaCommitCallback: making alter schema visible");
  state()->Finish();
}

string AlterSchemaOperation::ToString() const {
  return Substitute("AlterSchemaOperation [state=$0]", state()->ToString());
}

}  // namespace tablet
}  // namespace yb
