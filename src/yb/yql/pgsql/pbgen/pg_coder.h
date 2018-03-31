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
// Entry point for the protobuf code generating process.
// - Different from semantic phase, where each treenode is visited and processed individually and
//   isolatedly, PgCoder will visit multiple nodes while generating code.
//
// - Also different from semantic phase, where information is added to treenode, PgCoder only reads
//   the parse tree and outputs PgProto (code) without modifying the parse tree.
//
// - Compilation context is used to hold both parse tree, protobuf, and other supported structures.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_PGSQL_PBGEN_PG_CODER_H_
#define YB_YQL_PGSQL_PBGEN_PG_CODER_H_

#include "yb/common/ql_expr.h"
#include "yb/common/ql_rowblock.h"
#include "yb/common/partial_row.h"

#include "yb/yql/pgsql/proto/pg_proto.h"

#include "yb/yql/pgsql/ptree/pg_compile_context.h"
#include "yb/yql/pgsql/ptree/pg_tcreate_schema.h"
#include "yb/yql/pgsql/ptree/pg_tcreate_table.h"
#include "yb/yql/pgsql/ptree/pg_tdrop.h"

#include "yb/yql/pgsql/ptree/pg_tdml.h"
#include "yb/yql/pgsql/ptree/pg_tinsert.h"
#include "yb/yql/pgsql/ptree/pg_tdelete.h"
#include "yb/yql/pgsql/ptree/pg_tupdate.h"
#include "yb/yql/pgsql/ptree/pg_tselect.h"

namespace yb {
namespace pgsql {

class PgCoder {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<PgCoder> UniPtr;
  typedef std::unique_ptr<const PgCoder> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor & destructor.
  PgCoder();
  virtual ~PgCoder();

  void Reset();

  //------------------------------------------------------------------------------------------------
  // Generate code for parse tree.
  //------------------------------------------------------------------------------------------------
  CHECKED_STATUS Generate(const PgCompileContext::SharedPtr& compile_context,
                          PgProto::SharedPtr *pg_proto);

  // Generate protobuf for a treeNode. This function is the driver for traversing epxression tree.
  CHECKED_STATUS TRootToPB(const TreeNode *tnode);

  //------------------------------------------------------------------------------------------------
  // Codegen for statements.
  //------------------------------------------------------------------------------------------------

  // Insert statement.
  CHECKED_STATUS TStmtToPB(const PgTInsertStmt *tnode);

  // Delete statement.
  CHECKED_STATUS TStmtToPB(const PgTDeleteStmt *tnode) { return Status::OK(); }

  // Update statement.
  CHECKED_STATUS TStmtToPB(const PgTUpdateStmt *tnode) { return Status::OK(); }

  // Select statement.
  CHECKED_STATUS TStmtToPB(const PgTSelectStmt *tnode);

  //------------------------------------------------------------------------------------------------
  // Codegen for column arguments.
  //------------------------------------------------------------------------------------------------

  // Convert column references to protobuf.
  CHECKED_STATUS ColumnRefsToPB(const PgTDmlStmt *tnode, PgsqlColumnRefsPB *columns_pb);

  // Convert column arguments to protobuf.
  CHECKED_STATUS ColumnArgsToPB(const std::shared_ptr<client::YBTable>& table,
                                const PgTDmlStmt *tnode,
                                PgsqlWriteRequestPB *req);

  //------------------------------------------------------------------------------------------------
  // Codegen for column expressions.
  //------------------------------------------------------------------------------------------------

  CHECKED_STATUS TExprToPB(const PgTExpr::SharedPtr& expr, PgsqlExpressionPB *expr_pb);

  // Constant expressions.
  CHECKED_STATUS TConstToPB(const PgTExpr::SharedPtr& const_pt, QLValuePB *const_pb,
                            bool negate = false);

  CHECKED_STATUS TExprToPB(const PgTConstVarInt *const_pt, QLValuePB *const_pb, bool negate);

  CHECKED_STATUS TExprToPB(const PgTConstDecimal *const_pt, QLValuePB *const_pb, bool negate);

  CHECKED_STATUS TExprToPB(const PgTConstInt *const_pt, QLValuePB *const_pb, bool negate);

  CHECKED_STATUS TExprToPB(const PgTConstDouble *const_pt, QLValuePB *const_pb, bool negate);

  CHECKED_STATUS TExprToPB(const PgTConstText *const_pt, QLValuePB *const_pb);

  CHECKED_STATUS TExprToPB(const PgTConstBool *const_pt, QLValuePB *const_pb);

  CHECKED_STATUS TExprToPB(const PgTConstBinary *const_pt, QLValuePB *const_pb);

  // Column types.
  CHECKED_STATUS TExprToPB(const PgTRef *ref_pt, PgsqlExpressionPB *ref_pb);
  CHECKED_STATUS TExprToPB(const PgTAllColumns *ref_all, PgsqlReadRequestPB *req);

  // Builtin calls.
  // Even though BFCall and TSCall are processed similarly in executor at this point because they
  // have similar protobuf, it is best not to merge the two functions "BFCallToPB" and "TSCallToPB"
  // into one. That way, coding changes to one case doesn't affect the other in the future.
  CHECKED_STATUS TExprToPB(const PgTBcall *bcall_pt, PgsqlExpressionPB *bcall_pb);

  CHECKED_STATUS BFCallToPB(const PgTBcall *bcall_pt, PgsqlExpressionPB *expr_pb);

  CHECKED_STATUS TSCallToPB(const PgTBcall *bcall_pt, PgsqlExpressionPB *expr_pb);

#if 0
  //------------------------------------------------------------------------------------------------
  // Convert PTExpr to appropriate PgsqlExpressionPB with appropriate validation
  CHECKED_STATUS PTExprToPBValidated(const PgTExpr::SharedPtr& expr, PgsqlExpressionPB *expr_pb);

  //------------------------------------------------------------------------------------------------
  // Where clause evaluation.
  // Convert where clause to protobuf for read request.
  CHECKED_STATUS WhereClauseToPB(PgsqlReadRequestPB *req,
                                 const MCVector<ColumnOp>& key_where_ops,
                                 const MCList<ColumnOp>& where_ops,
                                 const MCList<SubscriptedColumnOp>& subcol_where_ops,
                                 const MCList<PartitionKeyOp>& partition_key_ops,
                                 const MCList<FuncOp>& func_ops,
                                 bool *no_results);

  // Convert where clause to protobuf for write request.
  CHECKED_STATUS WhereClauseToPB(PgsqlWriteRequestPB *req,
                                 const MCVector<ColumnOp>& key_where_ops,
                                 const MCList<ColumnOp>& where_ops,
                                 const MCList<SubscriptedColumnOp>& subcol_where_ops);

  // Convert an expression op in where clause to protobuf.
  CHECKED_STATUS WhereOpToPB(QLConditionPB *condition, const ColumnOp& col_op);
  CHECKED_STATUS WhereSubColOpToPB(QLConditionPB *condition, const SubscriptedColumnOp& subcol_op);
  CHECKED_STATUS FuncOpToPB(QLConditionPB *condition, const FuncOp& func_op);
#endif

 private:
  //------------------------------------------------------------------------------------------------
  PgCompileContext::SharedPtr compile_context_;

  // Generated code.
  PgProto::SharedPtr pg_proto_;
};

}  // namespace pgsql
}  // namespace yb

#endif  // YB_YQL_PGSQL_PBGEN_PG_CODER_H_
