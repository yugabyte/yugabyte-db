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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/cql/ql/exec/exec_context.h"
#include "yb/yql/cql/ql/ptree/pt_select.h"
#include "yb/client/callbacks.h"

namespace yb {
namespace ql {

ExecContext::ExecContext(const char *ql_stmt,
                         size_t stmt_len,
                         const ParseTree *parse_tree,
                         const StatementParameters *params,
                         QLEnv *ql_env)
    : ProcessContextBase(ql_stmt, stmt_len),
      parse_tree_(parse_tree),
      tnode_(parse_tree->root().get()),
      params_(params),
      start_time_(MonoTime::Now()),
      ql_env_(ql_env) {
}

ExecContext::ExecContext(const ExecContext& exec_context, const TreeNode *tnode)
    : ProcessContextBase(exec_context.stmt(), exec_context.stmt_len()),
      parse_tree_(exec_context.parse_tree_),
      tnode_(tnode),
      params_(exec_context.params_),
      start_time_(MonoTime::Now()),
      ql_env_(exec_context.ql_env_) {
}

ExecContext::~ExecContext() {
}

bool ExecContext::SelectingAggregate() {
  if (tnode()->opcode() == TreeNodeOpcode::kPTSelectStmt) {
    const PTSelectStmt *pt_select = static_cast<const PTSelectStmt*>(tnode());
    return pt_select->is_aggregate();
  }
  return false;
}

void ExecContext::InitializePartition(QLReadRequestPB *req, uint64_t start_partition) {
  current_partition_index_ = start_partition;
  // Hash values before the first 'IN' condition will be already set.
  // hash_values_options_ vector starts from the first column with an 'IN' restriction.
  // E.g. for a query "h1 = 1 and h2 in (2,3) and h3 in (4,5) and h4 = 6":
  // hashed_column_values() will be [1] and hash_values_options_ will be [[2,3],[4,5],[6]].
  int set_cols_size = req->hashed_column_values().size();
  int unset_cols_size = hash_values_options_->size();

  // Initialize the missing columns with default values (e.g. h2, h3, h4 in example above).
  req->mutable_hashed_column_values()->Reserve(set_cols_size + unset_cols_size);
  for (int i = 0; i < unset_cols_size; i++) {
    req->add_hashed_column_values();
  }

  // Set the right values for the missing/unset columns by converting partition index into positions
  // for each hash column and using the corresponding values from the hash values options vector.
  // E.g. In example above, with start_partition = 0:
  //    h4 = 6 since pos is "0 % 1 = 0", (start_position becomes 0 / 1 = 0).
  //    h3 = 4 since pos is "0 % 2 = 0", (start_position becomes 0 / 2 = 0).
  //    h2 = 2 since pos is "0 % 2 = 0", (start_position becomes 0 / 2 = 0).
  for (int i = unset_cols_size - 1; i >= 0; i--) {
    const auto& options = (*hash_values_options_)[i];
    int pos = start_partition % options.size();
    *req->mutable_hashed_column_values(i + set_cols_size) = options[pos];
    start_partition /= options.size();
  }
}

void ExecContext::AdvanceToNextPartition(QLReadRequestPB *req) {
  // E.g. for a query "h1 = 1 and h2 in (2,3) and h3 in (4,5) and h4 = 6" partition index 2:
  // this will do, index: 2 -> 3 and hashed_column_values(): [1, 3, 4, 6] -> [1, 3, 5, 6].
  current_partition_index_++;
  uint64_t partition_counter = current_partition_index_;
  // Hash_values_options_ vector starts from the first column with an 'IN' restriction.
  int hash_key_size = req->hashed_column_values().size();
  int fixed_cols_size = hash_key_size - hash_values_options_->size();

  // Set the right values for the missing/unset columns by converting partition index into positions
  // for each hash column and using the corresponding values from the hash values options vector.
  // E.g. In example above, with start_partition = 3:
  //    h4 = 6 since pos is "3 % 1 = 0", new partition counter is "3 / 1 = 3".
  //    h3 = 5 since pos is "3 % 2 = 1", pos is non-zero which guarantees previous cols don't need
  //    to be changed (i.e. are the same as for previous partition index) so we break.
  for (int i = hash_key_size - 1; i >= fixed_cols_size; i--) {
    const auto& options = (*hash_values_options_)[i - fixed_cols_size];
    int pos = partition_counter % options.size();
    *req->mutable_hashed_column_values(i) = options[pos];
    if (pos != 0) break; // The previous position hash values must be unchanged.
    partition_counter /= options.size();
  }
}

}  // namespace ql
}  // namespace yb
