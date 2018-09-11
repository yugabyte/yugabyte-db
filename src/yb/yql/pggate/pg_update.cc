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

#include "yb/yql/pggate/pg_update.h"

#include "yb/client/yb_op.h"

namespace yb {
namespace pggate {

using std::make_shared;
using std::shared_ptr;
using std::string;
using namespace std::literals;  // NOLINT

using client::YBClient;
using client::YBSession;
using client::YBMetaDataCache;
using client::YBTable;
using client::YBTableName;
using client::YBTableType;
using client::YBPgsqlWriteOp;

// TODO(neil) This should be derived from a GFLAGS.
static MonoDelta kSessionTimeout = 60s;

//--------------------------------------------------------------------------------------------------
// PgUpdate
//--------------------------------------------------------------------------------------------------

PgUpdate::PgUpdate(PgSession::ScopedRefPtr pg_session,
                   const char *database_name,
                   const char *schema_name,
                   const char *table_name)
    : PgDmlWrite(pg_session, database_name, schema_name, table_name, StmtOp::STMT_UPDATE) {
}

PgUpdate::~PgUpdate() {
}

void PgUpdate::AllocWriteRequest() {
  // Allocate WRITE operation.
  auto doc_op = make_shared<PgDocWriteOp>(pg_session_, table_desc_->NewPgsqlUpdate());
  write_req_ = doc_op->write_op()->mutable_request();

  // Preparation complete.
  doc_op_ = doc_op;
}

}  // namespace pggate
}  // namespace yb
