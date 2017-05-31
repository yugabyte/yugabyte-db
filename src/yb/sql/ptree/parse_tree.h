//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Parse Tree Declaration.
//
// This modules includes declarations for parse tree. The parser whose rules are defined in
// parser_gram.y will link the tree nodes together to form this parse tree.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PTREE_PARSE_TREE_H_
#define YB_SQL_PTREE_PARSE_TREE_H_

#include "yb/client/yb_table_name.h"
#include "yb/sql/ptree/tree_node.h"
#include "yb/sql/util/sql_env.h"

namespace yb {
namespace sql {

// Parse Tree
class ParseTree {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<ParseTree> UniPtr;
  typedef std::unique_ptr<const ParseTree> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Public functions.
  explicit ParseTree(std::shared_ptr<MemTracker> mem_tracker = nullptr);
  ~ParseTree();

  // Run semantics analysis.
  CHECKED_STATUS Analyze(SemContext *sem_context);

  // Access functions to root_.
  void set_root(const TreeNode::SharedPtr& root) {
    root_ = root;
  }
  const TreeNode::SharedPtr& root() const {
    return root_;
  }

  // Access function to ptree_mem_.
  MemoryContext *PTreeMem() const {
    return &ptree_mem_;
  }

  // Access function to psem_mem_.
  MemoryContext *PSemMem() const {
    return &psem_mem_;
  }

  // Add table to the set of tables used during semantic analysis.
  void AddAnalyzedTable(const client::YBTableName& table_name);

  // Clear the metadata cache of the tables used to analyze this parse tree.
  void ClearAnalyzedTableCache(SqlEnv *sql_env) const;

 private:
  std::shared_ptr<BufferAllocator> buffer_allocator_;

  // Set of tables used during semantic analysis.
  std::unordered_set<client::YBTableName, boost::hash<client::YBTableName>> analyzed_tables_;

  // Parse tree memory pool. This pool is used to allocate parse tree and its nodes. This pool
  // should be part of the generated parse tree that is stored within parse_context. Once the
  // parse tree is destructed, it's also gone.
  mutable Arena ptree_mem_;

  // Semantic analysis memory pool. This pool is used to allocate memory for storing semantic
  // analysis results in the parse tree. When a parse tree is analyzed, the parse tree is reset to
  // release the previous analysis result and this pool should be reset to free the associated
  // memory. This pool should be part of the generated parse tree also that is stored within
  // sem_context. Once the parse tree is destructed, it's also gone too.
  mutable Arena psem_mem_;

  //------------------------------------------------------------------------------------------------
  // Private data members.
  TreeNode::SharedPtr root_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_PARSE_TREE_H_
