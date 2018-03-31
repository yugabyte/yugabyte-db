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
// Structure for generated code.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_PGSQL_PROTO_PG_PROTO_H_
#define YB_YQL_PGSQL_PROTO_PG_PROTO_H_

#include "yb/client/client.h"
#include "yb/yql/pgsql/ptree/parse_tree.h"

namespace yb {
namespace pgsql {

class PgProto {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::shared_ptr<PgProto> SharedPtr;
  typedef std::shared_ptr<const PgProto> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor & destructor.
  explicit PgProto(const string& stmt);
  virtual ~PgProto();

  const string& stmt() {
    return stmt_;
  }

  // Get DDL statement for execution.
  const TreeNode *ddl_stmt() {
    return ddl_stmt_;
  }

  // Get write_op generated code.
  std::shared_ptr<client::YBPgsqlWriteOp> write_op() const {
    return write_op_;
  }

  // Get read_op generated code.
  std::shared_ptr<client::YBPgsqlReadOp> read_op() const {
    return read_op_;
  }

  // Access function to ptree_mem_.
  MemoryContext *ProtoMem() const {
    return &proto_mem_;
  }

  // Setup execution code for DDL.
  void SetDdlStmt(const ParseTree::SharedPtr& parse_tree, const TreeNode *ddl_stmt);

  // Setup execution code for DML write.
  void SetDmlWriteOp(const std::shared_ptr<client::YBPgsqlWriteOp>& write_op);

  // Setup execution code for DML read.
  void SetDmlReadOp(const std::shared_ptr<client::YBPgsqlReadOp>& read_op);

 private:
  // Client text statement.
  const string stmt_;

  // For DDL statements, the code will just be the tree_node itself, at least for now.
  // - In this case, we need to keep the parse tree around until the execution is done.
  // - We'll never cache DDL. ProxyServer executes directly on tree node.
  // - In the future, if it makes sense, we can also generate ProtoBuf for DDL.
  ParseTree::SharedPtr parse_tree_;
  const TreeNode *ddl_stmt_ = nullptr;

  // For DML statements, we generates protobuf and send it to the tablet servers.
  std::shared_ptr<client::YBPgsqlWriteOp> write_op_;
  std::shared_ptr<client::YBPgsqlReadOp> read_op_;

  // TODO(neil) Use code memory pool.
  // This pool should be used for generated code. Once the code is gone, the pool is also gone.
  // However, it seems our Arena cannot be used to create protobuf yet.
  mutable Arena proto_mem_;
};

}  // namespace pgsql
}  // namespace yb

#endif  // YB_YQL_PGSQL_PROTO_PG_PROTO_H_
