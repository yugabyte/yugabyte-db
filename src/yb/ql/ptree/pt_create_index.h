//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Tree node definitions for CREATE INDEX statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_QL_PTREE_PT_CREATE_INDEX_H_
#define YB_QL_PTREE_PT_CREATE_INDEX_H_

#include "yb/ql/ptree/pt_create_table.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------
// CREATE INDEX statement.

class PTCreateIndex : public PTCreateTable {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTCreateIndex> SharedPtr;
  typedef MCSharedPtr<const PTCreateIndex> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTCreateIndex(MemoryContext *memctx,
                YBLocation::SharedPtr loc,
                const MCSharedPtr<MCString>& name,
                const PTQualifiedName::SharedPtr& table_name,
                const PTListNode::SharedPtr& columns,
                bool create_if_not_exists,
                const PTTablePropertyListNode::SharedPtr& ordering_list,
                const PTListNode::SharedPtr& covering);
  virtual ~PTCreateIndex();

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTCreateIndex;
  }

  // Support for shared_ptr.
  template<typename... TypeArgs>
  inline static PTCreateIndex::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTCreateIndex>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Accessor methods.
  const MCSharedPtr<MCString>& name() const {
    return name_;
  }
  const PTListNode::SharedPtr& covering() const {
    return covering_;
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override;
  void PrintSemanticAnalysisResult(SemContext *sem_context);

 private:
  // Index name.
  const MCSharedPtr<MCString> name_;
  // Additional covering columns.
  const PTListNode::SharedPtr covering_;

  // The semantic analyzer will decorate the following information.
  std::shared_ptr<client::YBTable> table_;
  MCVector<ColumnDesc> column_descs_;
  MCVector<PTColumnDefinition::SharedPtr> column_definitions_;
  int num_key_columns_ = 0;
  int num_hash_key_columns_ = 0;
};

}  // namespace ql
}  // namespace yb

#endif  // YB_QL_PTREE_PT_CREATE_INDEX_H_
