//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Tree node definitions for INSERT statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PTREE_PT_DML_H_
#define YB_SQL_PTREE_PT_DML_H_

#include "yb/client/client.h"

#include "yb/sql/ptree/column_desc.h"
#include "yb/sql/ptree/list_node.h"
#include "yb/sql/ptree/tree_node.h"
#include "yb/sql/ptree/pt_expr.h"
#include "yb/sql/ptree/column_arg.h"
#include "yb/common/ttl_constants.h"

namespace yb {
namespace sql {

// This class represents the data of collection type. PostgreSql syntax rules dictate how we form
// the hierarchy of our C++ classes, so classes for VALUES and SELECT clause must share the same
// base class.
// - VALUES (x, y, z)
// - (SELECT x, y, z FROM tab)
// Functionalities of this class should be "protected" to make sure that PTCollection instances are
// not created and used by application.
class PTCollection : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTCollection> SharedPtr;
  typedef MCSharedPtr<const PTCollection> SharedPtrConst;

 protected:
  //------------------------------------------------------------------------------------------------
  // Constructor and destructor. Define them in protected section to prevent application from
  // declaring them.
  PTCollection(MemoryContext *memctx, YBLocation::SharedPtr loc)
      : TreeNode(memctx, loc) {
  }
  virtual ~PTCollection() {
  }
};

//--------------------------------------------------------------------------------------------------

class PTDmlStmt : public PTCollection {
 public:
  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  explicit PTDmlStmt(MemoryContext *memctx,
                     YBLocation::SharedPtr loc,
                     bool write_only,
                     PTConstInt::SharedPtr ttl_seconds = nullptr);
  virtual ~PTDmlStmt();

  template<typename... TypeArgs>
  inline static PTDmlStmt::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTDmlStmt>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Lookup table from the metadata database.
  CHECKED_STATUS LookupTable(SemContext *sem_context);

  // Semantic-analyzing the where clause.
  CHECKED_STATUS AnalyzeWhereClause(SemContext *sem_context, const PTExpr::SharedPtr& where_clause);

  // Semantic-analyzing the if clause.
  CHECKED_STATUS AnalyzeIfClause(SemContext *sem_context, const PTExpr::SharedPtr& if_clause);

  // Table name.
  virtual const char *table_name() const = 0;

  // Returns location of table name.
  virtual const YBLocation& table_loc() const = 0;

  // Access functions.
  const std::shared_ptr<client::YBTable>& table() const {
    return table_;
  }

  int num_columns() const {
    return table_columns_.size();
  }

  int num_key_columns() const {
    return num_key_columns_;
  }

  int num_hash_key_columns() const {
    return num_hash_key_columns_;
  }

  const MCVector<ColumnOp>& key_where_ops() const {
    return key_where_ops_;
  }

  const MCList<ColumnOp>& where_ops() const {
    return where_ops_;
  }

  bool has_ttl() const {
    return ttl_seconds_ != nullptr;
  }

  int64_t ttl_msec() const {
    CHECK_NOTNULL(ttl_seconds_.get());
    return ttl_seconds_->Eval() * MonoTime::kMillisecondsPerSecond;
  }

  // Access for column_args.
  const MCVector<ColumnArg>& column_args() const {
    return column_args_;
  }

 protected:
  // Data types.
  struct WhereSemanticStats {
    WhereSemanticStats() : has_gt_(false), has_lt_(false), has_eq_(false) {
    }

    bool has_gt_;
    bool has_lt_;
    bool has_eq_;
  };

  // Protected functions.
  CHECKED_STATUS AnalyzeWhereExpr(SemContext *sem_context,
                                  PTExpr *expr,
                                  MCVector<WhereSemanticStats> *col_stats);
  CHECKED_STATUS AnalyzeIfExpr(SemContext *sem_context,
                               PTExpr *expr);
  CHECKED_STATUS AnalyzeCompareExpr(SemContext *sem_context,
                                    PTExpr *expr,
                                    const ColumnDesc **col_desc = nullptr,
                                    PTExpr::SharedPtr *value = nullptr);
  CHECKED_STATUS AnalyzeBetweenExpr(SemContext *sem_context,
                                    PTExpr *expr);
  CHECKED_STATUS AnalyzeColumnExpr(SemContext *sem_context,
                                   PTExpr *expr);

  // Semantic-analyzing the USING TTL clause.
  CHECKED_STATUS AnalyzeUsingClause(SemContext *sem_context);


  // The sematic analyzer will decorate this node with the following information.
  std::shared_ptr<client::YBTable> table_;

  // Table columns.
  MCVector<ColumnDesc> table_columns_;
  int num_key_columns_;
  int num_hash_key_columns_;

  // Where operator list.
  // - When reading (SELECT), key_where_ops_ has only HASH (partition) columns.
  // - When writing (UPDATE & DELETE), key_where_ops_ has both has (partition) & range columns.
  // This is just a workaround for UPDATE and DELETE. Backend supports only single row. It also
  // requires that conditions on columns are ordered the same way as they were defined in
  // CREATE TABLE statement.
  MCVector<ColumnOp> key_where_ops_;
  MCList<ColumnOp> where_ops_;

  // Predicate for write operator (UPDATE & DELETE).
  bool write_only_;
  PTConstInt::SharedPtr ttl_seconds_;

  // Semantic phase will decorate the following field.
  MCVector<ColumnArg> column_args_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_PT_DML_H_
