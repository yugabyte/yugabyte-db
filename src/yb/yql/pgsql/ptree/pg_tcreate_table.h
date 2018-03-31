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
//
// Tree node definitions for CREATE TABLE statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_PGSQL_PTREE_PG_TCREATE_TABLE_H_
#define YB_YQL_PGSQL_PTREE_PG_TCREATE_TABLE_H_

#include "yb/common/schema.h"
#include "yb/master/master.pb.h"
#include "yb/yql/pgsql/ptree/list_node.h"
#include "yb/yql/pgsql/ptree/tree_node.h"
#include "yb/yql/pgsql/ptree/pg_ttype.h"
#include "yb/yql/pgsql/ptree/pg_tname.h"
#include "yb/yql/pgsql/ptree/pg_tupdate.h"

namespace yb {
namespace pgsql {

//--------------------------------------------------------------------------------------------------
// Constraints.

enum class PgTConstraintType {
  kNone = 0,
  kPrimaryKey,
  kUnique,
  kNotNull,
};

class PgTConstraint : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PgTConstraint> SharedPtr;
  typedef MCSharedPtr<const PgTConstraint> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  explicit PgTConstraint(MemoryContext *memctx = nullptr, PgTLocation::SharedPtr loc = nullptr)
      : TreeNode(memctx, loc) {
  }
  virtual ~PgTConstraint() {
  }

  virtual PgTConstraintType constraint_type() = 0;
};

class PgTPrimaryKey : public PgTConstraint {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PgTPrimaryKey> SharedPtr;
  typedef MCSharedPtr<const PgTPrimaryKey> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PgTPrimaryKey(MemoryContext *memctx,
               PgTLocation::SharedPtr loc,
               const PTListNode::SharedPtr& columns_ = nullptr);
  virtual ~PgTPrimaryKey();

  virtual PgTConstraintType constraint_type() override {
    return PgTConstraintType::kPrimaryKey;
  }

  template<typename... TypeArgs>
  inline static PgTPrimaryKey::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PgTPrimaryKey>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(PgCompileContext *compile_context) override;

  // Predicate whether this PTPrimary node is a column constraint or a table constraint.
  // - Besides the datatype, certain constraints can also be specified when defining a column in
  //   the table. Those constraints are column constraints. The following key is column constraint.
  //     CREATE TABLE t(i int primary key, j int);
  //
  // - When creating table, besides column definitions, other elements of the table can also be
  //   specified. Those elements are table constraints. The following key is table constraint.
  //     CREATE TABLE t(i int, j int, primary key(i));
  bool is_table_element() const {
    return columns_ != nullptr;
  }

  bool is_column_element() const {
    return columns_ == nullptr;
  }

 private:
  PTListNode::SharedPtr columns_;
};

//--------------------------------------------------------------------------------------------------
// Table column.

class PgTColumnDefinition : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PgTColumnDefinition> SharedPtr;
  typedef MCSharedPtr<const PgTColumnDefinition> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PgTColumnDefinition(MemoryContext *memctx,
                      PgTLocation::SharedPtr loc,
                      const MCSharedPtr<MCString>& name,
                      const PgTBaseType::SharedPtr& datatype,
                      const PTListNode::SharedPtr& qualifiers);
  virtual ~PgTColumnDefinition();

  template<typename... TypeArgs>
  inline static PgTColumnDefinition::SharedPtr MakeShared(MemoryContext *memctx,
                                                         TypeArgs&&... args) {
    return MCMakeShared<PgTColumnDefinition>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(PgCompileContext *compile_context) override;

  // Access function for is_primary_key_.
  bool is_primary_key() const {
    return is_primary_key_;
  }
  void set_is_primary_key() {
    is_primary_key_ = true;
  }

  // Access function for is_hash_key_.
  bool is_hash_key() const {
    return is_hash_key_;
  }
  void set_is_hash_key() {
    is_primary_key_ = true;
    is_hash_key_ = true;
  }

  // Access function for order_.
  int32_t order() const {
    return order_;
  }
  void set_order(int32 order) {
    order_ = order;
  }

  ColumnSchema::SortingType sorting_type() {
    return sorting_type_;
  }

  void set_sorting_type(ColumnSchema::SortingType sorting_type) {
    sorting_type_ = sorting_type;
  }

  const char *yb_name() const {
    return name_->c_str();
  }

  const PgTBaseType::SharedPtr& datatype() const {
    return datatype_;
  }

  std::shared_ptr<QLType> ql_type() const {
    return datatype_->ql_type();
  }

 private:
  const MCSharedPtr<MCString> name_;
  PgTBaseType::SharedPtr datatype_;
  PTListNode::SharedPtr qualifiers_;
  bool is_primary_key_;
  bool is_hash_key_;
  int32_t order_;
  // Sorting order. Only relevant when this key is a primary key.
  ColumnSchema::SortingType sorting_type_;
};

//--------------------------------------------------------------------------------------------------
// CREATE TABLE statement.

class PgTCreateTable : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PgTCreateTable> SharedPtr;
  typedef MCSharedPtr<const PgTCreateTable> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PgTCreateTable(MemoryContext *memctx,
                 PgTLocation::SharedPtr loc,
                 const PgTQualifiedName::SharedPtr& name,
                 const PTListNode::SharedPtr& elements,
                 bool create_if_not_exists);
  virtual ~PgTCreateTable();

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPgTCreateTable;
  }

  // Support for shared_ptr.
  template<typename... TypeArgs>
  inline static PgTCreateTable::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PgTCreateTable>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(PgCompileContext *compile_context) override;
  void PrintSemanticAnalysisResult(PgCompileContext *compile_context);

  // column lists.
  const PTListNode::SharedPtr& elements() const {
    return elements_;
  }

  const MCList<PgTColumnDefinition *>& columns() const {
    return columns_;
  }

  const MCList<PgTColumnDefinition *>& primary_columns() const {
    return primary_columns_;
  }

  const MCList<PgTColumnDefinition *>& hash_columns() const {
    return hash_columns_;
  }

  bool create_if_not_exists() const {
    return create_if_not_exists_;
  }

  CHECKED_STATUS AppendColumn(PgCompileContext *compile_context,
                              PgTColumnDefinition *column);

  CHECKED_STATUS AppendPrimaryColumn(PgCompileContext *compile_context,
                                     PgTColumnDefinition *column);

  static CHECKED_STATUS CheckType(PgCompileContext *compile_context,
                                  const PgTBaseType::SharedPtr& datatype);

  static CHECKED_STATUS CheckPrimaryType(PgCompileContext *compile_context,
                                         const PgTBaseType::SharedPtr& datatype);

  // Table name.
  PgTQualifiedName::SharedPtr table_name() const {
    return relation_;
  }
  virtual client::YBTableName yb_table_name() const {
    return relation_->ToTableName();
  }

 protected:
  PgTQualifiedName::SharedPtr relation_;
  PTListNode::SharedPtr elements_;
  PgTColumnDefinition::SharedPtr col_oid_;
  PgTColumnDefinition::SharedPtr col_ctid_;

  MCList<PgTColumnDefinition *> columns_;
  MCList<PgTColumnDefinition *> primary_columns_;
  MCList<PgTColumnDefinition *> hash_columns_;

  bool create_if_not_exists_;
};

}  // namespace pgsql
}  // namespace yb

#endif  // YB_YQL_PGSQL_PTREE_PG_TCREATE_TABLE_H_
