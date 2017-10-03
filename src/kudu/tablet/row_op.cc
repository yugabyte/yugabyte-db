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

#include "kudu/common/wire_protocol.h"
#include "kudu/tablet/row_op.h"
#include "kudu/tablet/tablet.pb.h"

namespace kudu {
namespace tablet {

RowOp::RowOp(DecodedRowOperation decoded_op)
    : decoded_op(std::move(decoded_op)) {}

RowOp::~RowOp() {
}

void RowOp::SetFailed(const Status& s) {
  DCHECK(!result) << result->DebugString();
  result.reset(new OperationResultPB());
  StatusToPB(s, result->mutable_failed_status());
}

void RowOp::SetInsertSucceeded(int mrs_id) {
  DCHECK(!result) << result->DebugString();
  result.reset(new OperationResultPB());
  result->add_mutated_stores()->set_mrs_id(mrs_id);
}

void RowOp::SetMutateSucceeded(gscoped_ptr<OperationResultPB> result) {
  DCHECK(!this->result) << result->DebugString();
  this->result = result.Pass();
}

string RowOp::ToString(const Schema& schema) const {
  return decoded_op.ToString(schema);
}

void RowOp::SetAlreadyFlushed() {
  DCHECK(!result) << result->DebugString();
  result.reset(new OperationResultPB());
  result->set_flushed(true);
}

} // namespace tablet
} // namespace kudu
