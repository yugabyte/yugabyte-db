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
// PgInsert
//--------------------------------------------------------------------------------------------------

PgInsert::PgInsert(PgSession::ScopedRefPtr pg_session,
                   const char *database_name,
                   const char *schema_name,
                   const char *table_name)
    : PgDmlWrite(pg_session, database_name, schema_name, table_name, StmtOp::STMT_INSERT) {
}

PgInsert::~PgInsert() {
}

Status PgInsert::Prepare() {
  RETURN_NOT_OK(PgDmlWrite::Prepare());

  // If the table contains ybrowid, bind generate_rowid() to auto-fill the rowid.
  const auto rowid_attr_num = static_cast<int>(PgSystemAttrNum::kYBRowIdAttributeNumber);
  if (table_desc_->FindColumn(rowid_attr_num)) {
    generate_rowid_ = std::make_unique<PgGenerateRowId>();
    RETURN_NOT_OK(BindColumn(rowid_attr_num, generate_rowid_.get()));
  }

  return Status::OK();
}

void PgInsert::AllocWriteRequest() {
  // Allocate WRITE operation.
  auto doc_op = make_shared<PgDocWriteOp>(pg_session_, table_desc_->NewPgsqlInsert());
  write_req_ = doc_op->write_op()->mutable_request();

  // Preparation complete.
  doc_op_ = doc_op;
}

}  // namespace pggate
}  // namespace yb
