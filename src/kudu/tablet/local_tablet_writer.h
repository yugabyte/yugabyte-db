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
#ifndef KUDU_TABLET_LOCAL_TABLET_WRITER_H
#define KUDU_TABLET_LOCAL_TABLET_WRITER_H

#include <vector>

#include "kudu/common/partial_row.h"
#include "kudu/common/row_operations.h"
#include "kudu/consensus/log_anchor_registry.h"
#include "kudu/consensus/opid_util.h"
#include "kudu/tablet/row_op.h"
#include "kudu/tablet/tablet.h"
#include "kudu/tablet/transactions/write_transaction.h"
#include "kudu/gutil/macros.h"

namespace kudu {
namespace tablet {

// Helper class to write directly into a local tablet, without going
// through TabletPeer, consensus, etc.
//
// This is useful for unit-testing the Tablet code paths with no consensus
// implementation or thread pools.
class LocalTabletWriter {
 public:
  struct Op {
    Op(RowOperationsPB::Type type,
       const KuduPartialRow* row)
      : type(type),
        row(row) {
    }

    RowOperationsPB::Type type;
    const KuduPartialRow* row;
  };

  explicit LocalTabletWriter(Tablet* tablet,
                             const Schema* client_schema)
    : tablet_(tablet),
      client_schema_(client_schema) {
    CHECK(!client_schema->has_column_ids());
    CHECK_OK(SchemaToPB(*client_schema, req_.mutable_schema()));
  }

  ~LocalTabletWriter() {}

  Status Insert(const KuduPartialRow& row) {
    return Write(RowOperationsPB::INSERT, row);
  }

  Status Delete(const KuduPartialRow& row) {
    return Write(RowOperationsPB::DELETE, row);
  }

  Status Update(const KuduPartialRow& row) {
    return Write(RowOperationsPB::UPDATE, row);
  }

  // Perform a write against the local tablet.
  // Returns a bad Status if the applied operation had a per-row error.
  Status Write(RowOperationsPB::Type type,
               const KuduPartialRow& row) {
    vector<Op> ops;
    ops.push_back(Op(type, &row));
    return WriteBatch(ops);
  }

  Status WriteBatch(const std::vector<Op>& ops) {
    req_.mutable_row_operations()->Clear();
    RowOperationsPBEncoder encoder(req_.mutable_row_operations());

    for (const Op& op : ops) {
      encoder.Add(op.type, *op.row);
    }

    tx_state_.reset(new WriteTransactionState(NULL, &req_, NULL));

    RETURN_NOT_OK(tablet_->DecodeWriteOperations(client_schema_, tx_state_.get()));
    RETURN_NOT_OK(tablet_->AcquireRowLocks(tx_state_.get()));
    tablet_->StartTransaction(tx_state_.get());

    // Create a "fake" OpId and set it in the TransactionState for anchoring.
    tx_state_->mutable_op_id()->CopyFrom(consensus::MaximumOpId());
    tablet_->ApplyRowOperations(tx_state_.get());

    tx_state_->ReleaseTxResultPB(&result_);
    tx_state_->Commit();
    tx_state_->release_row_locks();
    tx_state_->ReleaseSchemaLock();

    // Return the status of first failed op.
    int op_idx = 0;
    for (const OperationResultPB& result : result_.ops()) {
      if (result.has_failed_status()) {
        return StatusFromPB(result.failed_status())
          .CloneAndPrepend(ops[op_idx].row->ToString());
        break;
      }
      op_idx++;
    }
    return Status::OK();
  }

  // Return the result of the last row operation run against the tablet.
  const OperationResultPB& last_op_result() {
    CHECK_GE(result_.ops_size(), 1);
    return result_.ops(result_.ops_size() - 1);
  }

 private:
  Tablet* const tablet_;
  const Schema* client_schema_;

  TxResultPB result_;
  tserver::WriteRequestPB req_;
  gscoped_ptr<WriteTransactionState> tx_state_;

  DISALLOW_COPY_AND_ASSIGN(LocalTabletWriter);
};


} // namespace tablet
} // namespace kudu
#endif /* KUDU_TABLET_LOCAL_TABLET_WRITER_H */
