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
// Parse Tree Declaration.
//
// This modules includes declarations for parse tree. The parser whose rules are defined in
// parser_gram.y will link the tree nodes together to form this parse tree.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_PGSQL_PTREE_PARSE_TREE_H_
#define YB_YQL_PGSQL_PTREE_PARSE_TREE_H_

#include "yb/client/yb_table_name.h"
#include "yb/yql/pgsql/ptree/tree_node.h"
#include "yb/yql/pgsql/util/pg_env.h"

namespace yb {
namespace pgsql {

// Parse Tree
class ParseTree {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::shared_ptr<ParseTree> SharedPtr;
  typedef std::shared_ptr<const ParseTree> SharedPtrConst;

  typedef std::unique_ptr<ParseTree> UniPtr;
  typedef std::unique_ptr<const ParseTree> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Public functions.
  ParseTree();
  ~ParseTree();

  // Run semantics analysis.
  CHECKED_STATUS Analyze(PgCompileContext *compile_context);

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

 private:
  std::shared_ptr<BufferAllocator> buffer_allocator_;

  // Parse tree memory pool. This pool is used to allocate parse tree and its nodes. This pool
  // should be part of the generated parse tree that is stored within parse_context. Once the
  // parse tree is destructed, it's also gone.
  mutable Arena ptree_mem_;

  // Semantic analysis memory pool. This pool is used to allocate memory for storing semantic
  // analysis results in the parse tree. When a parse tree is analyzed, the parse tree is reset to
  // release the previous analysis result and this pool should be reset to free the associated
  // memory. This pool should be part of the generated parse tree also that is stored within
  // compile_context. Once the parse tree is destructed, it's also gone too.
  mutable Arena psem_mem_;

  //------------------------------------------------------------------------------------------------
  // Private data members.
  TreeNode::SharedPtr root_;
};

}  // namespace pgsql
}  // namespace yb

#endif  // YB_YQL_PGSQL_PTREE_PARSE_TREE_H_
