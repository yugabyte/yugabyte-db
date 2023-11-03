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

#pragma once

#include <functional>
#include <memory>

#include "yb/client/yb_table_name.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/util/mem_tracker.h"
#include "yb/util/memory/arena.h"

#include "yb/yql/cql/ql/ptree/tree_node.h"
#include "yb/yql/cql/ql/util/ql_env.h"

namespace yb {
namespace ql {

// Parse Tree
class ParseTree {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<ParseTree> UniPtr;
  typedef std::unique_ptr<const ParseTree> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Public functions.

  // Constructs a parse tree. The parse tree saves a reference to the statement string.
  ParseTree(const std::string& stmt, bool reparsed, const MemTrackerPtr& mem_tracker = nullptr,
            const bool internal = false);
  ~ParseTree();

  // Run semantics analysis.
  Status Analyze(SemContext *sem_context);

  // Access function for stmt_.
  const std::string& stmt() const {
    return stmt_;
  }

  // Access function to reparsed_.
  bool reparsed() const {
    return reparsed_;
  }
  void clear_reparsed() const {
    reparsed_ = false;
  }

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

  // Access function to stale_.
  bool stale() const {
    return stale_;
  }

  void set_stale() const {
    stale_ = true;
  }

  bool internal() const {
    return internal_;
  }

  // Get schema version this statement used.
  Result<SchemaVersion> GetYBTableSchemaVersion() const;

  // Check if the used schema version is not in sync with the Master.
  Result<bool> IsYBTableAltered(QLEnv *ql_env, bool use_cache) const;

  // Add table to the set of tables used during semantic analysis.
  void AddAnalyzedTable(const client::YBTableName& table_name);

  // Clear the metadata cache of the tables used to analyze this parse tree.
  void ClearAnalyzedTableCache(QLEnv *ql_env) const;

  // Add type to the set of types used during semantic analysis.
  void AddAnalyzedUDType(const std::string& keyspace_name, const std::string& type_name);

  // Clear the metadata cache of the types used to analyze this parse tree.
  void ClearAnalyzedUDTypeCache(QLEnv *ql_env) const;

 private:
  static std::shared_ptr<const client::YBTable> GetYBTableFromTreeNode(const TreeNode *tnode);

  // The SQL statement.
  const std::string& stmt_;

  // Has this statement been reparsed?
  mutable std::atomic<bool> reparsed_ = {false};

  std::shared_ptr<BufferAllocator> buffer_allocator_;

  // Set of tables used during semantic analysis.
  std::unordered_set<client::YBTableName, boost::hash<client::YBTableName>> analyzed_tables_;

  // Set of types used during semantic analysis.
  std::unordered_set<std::pair<std::string, std::string>,
                     boost::hash<std::pair<std::string, std::string>>> analyzed_types_;

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

  // Root node of the parse tree.
  TreeNode::SharedPtr root_;

  // Is this parse tree stale?
  mutable std::atomic<bool> stale_ = {false};

  // Was this generated internally? Used to to bypass authorization enforcement.
  bool internal_ = false;
};

}  // namespace ql
}  // namespace yb
