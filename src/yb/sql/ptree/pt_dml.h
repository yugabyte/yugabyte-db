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
                     PTOptionExist option_exists = PTOptionExist::DEFAULT);
  virtual ~PTDmlStmt();

  template<typename... TypeArgs>
  inline static PTDmlStmt::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTDmlStmt>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Lookup table from the metadata database.
  CHECKED_STATUS LookupTable(SemContext *sem_context);

  // Semantic-analyzing the where clause.
  CHECKED_STATUS AnalyzeWhereClause(SemContext *sem_context, const PTExpr::SharedPtr& where_clause);

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

  const MCVector<ColumnOp>& hash_where_ops() const {
    return hash_where_ops_;
  }

  const MCList<ColumnOp>& where_ops() const {
    return where_ops_;
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
  CHECKED_STATUS AnalyzeWhereCompareExpr(SemContext *sem_context,
                                         PTExpr *expr,
                                         const ColumnDesc **col_desc,
                                         PTExpr::SharedPtr *value);

  // DML statement options.
  PTOptionExist option_exists_;

  // The sematic analyzer will decorate this node with the following information.
  std::shared_ptr<client::YBTable> table_;

  // Table columns.
  MCVector<ColumnDesc> table_columns_;
  int num_key_columns_;
  int num_hash_key_columns_;

  // Where operator list.
  MCVector<ColumnOp> hash_where_ops_;
  MCList<ColumnOp> where_ops_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_PT_DML_H_
