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
// Protobuf Code Implementation.
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pgsql/proto/pg_proto.h"

namespace yb {
namespace pgsql {

using std::make_shared;
using std::shared_ptr;

using client::YBPgsqlWriteOp;
using client::YBPgsqlReadOp;

PgProto::PgProto(const string& stmt) : stmt_(stmt) {
}

PgProto::~PgProto() {
}

// Setup execution code for DDL.
void PgProto::SetDdlStmt(const ParseTree::SharedPtr& parse_tree, const TreeNode *ddl_stmt) {
  parse_tree_ = parse_tree;
  ddl_stmt_ = ddl_stmt;
}

// Setup execution code for DML write.
void PgProto::SetDmlWriteOp(const shared_ptr<YBPgsqlWriteOp>& write_op) {
  write_op_ = write_op;
}

// Setup execution code for DML read.
void PgProto::SetDmlReadOp(const shared_ptr<YBPgsqlReadOp>& read_op) {
  read_op_ = read_op;
}

}  // namespace pgsql
}  // namespace yb
