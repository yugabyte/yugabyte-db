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

#pragma once

#include <mutex>
#include <string>

#include "yb/qlexpr/index.h"

#include "yb/consensus/log_fwd.h"

#include "yb/gutil/macros.h"

#include "yb/tablet/operations.messages.h"
#include "yb/tablet/operations/operation.h"

#include "yb/tserver/tserver_fwd.h"
#include "yb/tserver/tserver_admin.pb.h"

#include "yb/util/locks.h"

namespace yb {

class Schema;

namespace tablet {

class TabletPeer;

// Operation Context for the AlterSchema operation.
// Keeps track of the Operation states (request, result, ...)
class ChangeMetadataOperation
    : public ExclusiveSchemaOperation<OperationType::kChangeMetadata,
                                      LWChangeMetadataRequestPB> {
 public:
  ChangeMetadataOperation(TabletPtr tablet, log::Log* log,
                          const LWChangeMetadataRequestPB* request = nullptr);

  explicit ChangeMetadataOperation(const LWChangeMetadataRequestPB* request);

  ~ChangeMetadataOperation();

  void set_schema(const Schema* schema) { schema_ = schema; }
  const Schema* schema() const { return schema_; }

  void SetIndexes(const google::protobuf::RepeatedPtrField<IndexInfoPB>& indexes);

  qlexpr::IndexMap& index_map() {
    return index_map_;
  }

  Slice new_table_name() const {
    return request()->new_table_name();
  }

  bool has_new_table_name() const {
    return request()->has_new_table_name();
  }

  uint32_t schema_version() const {
    return request()->schema_version();
  }

  uint32_t wal_retention_secs() const {
    return request()->wal_retention_secs();
  }

  bool has_wal_retention_secs() const {
    return request()->has_wal_retention_secs();
  }

  bool has_table_id() const {
    return request()->has_alter_table_id();
  }

  Slice table_id() const {
    return request()->alter_table_id();
  }

  log::Log* log() const { return log_; }

  log::Log* mutable_log() { return log_; }

  virtual std::string ToString() const override;

  // Executes a Prepare for the metadata change operation.
  //
  // TODO: need a schema lock?

  Status Prepare(IsLeaderSide is_leader_side) override;

  Status Apply(int64_t leader_term, Status* complete_status);

 private:

  // Starts the ChangeMetadataOperation by assigning it a timestamp.
  Status DoReplicated(int64_t leader_term, Status* complete_status) override;

  Status DoAborted(const Status& status) override;

  log::Log* const log_;

  // The new (target) Schema.
  const Schema* schema_ = nullptr;
  std::unique_ptr<Schema> schema_holder_;

  // Lookup map for the associated indexes.
  qlexpr::IndexMap index_map_;
};

Status SyncReplicateChangeMetadataOperation(
    const ChangeMetadataRequestPB* req,
    tablet::TabletPeer* tablet_peer,
    int64_t term);

}  // namespace tablet
}  // namespace yb
