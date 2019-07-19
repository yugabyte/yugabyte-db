//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pggate/pg_insert.h"
#include "yb/client/yb_op.h"

namespace yb {
namespace pggate {

using std::make_shared;
using std::shared_ptr;
using std::string;
using namespace std::literals;  // NOLINT

// TODO(neil) This should be derived from a GFLAGS.
static MonoDelta kSessionTimeout = 60s;

//--------------------------------------------------------------------------------------------------
// PgInsert
//--------------------------------------------------------------------------------------------------

PgInsert::PgInsert(PgSession::ScopedRefPtr pg_session,
                   const PgObjectId& table_id,
                   bool is_single_row_txn)
    : PgDmlWrite(pg_session, table_id, is_single_row_txn) {
}

PgInsert::~PgInsert() {
}

void PgInsert::AllocWriteRequest() {
  // Allocate WRITE operation.
  client::YBPgsqlWriteOp *insert_op = table_desc_->NewPgsqlInsert();
  insert_op->set_is_single_row_txn(is_single_row_txn_);
  auto doc_op = make_shared<PgDocWriteOp>(pg_session_, insert_op);
  write_req_ = doc_op->write_op()->mutable_request();

  // Preparation complete.
  doc_op_ = doc_op;
}

}  // namespace pggate
}  // namespace yb
