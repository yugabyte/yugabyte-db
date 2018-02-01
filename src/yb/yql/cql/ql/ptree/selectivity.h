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
// Selectivity class for SELECT statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_CQL_QL_PTREE_SELECTIVITY_H
#define YB_YQL_CQL_QL_PTREE_SELECTIVITY_H

#include "yb/util/memory/mc_types.h"
#include "yb/common/index.h"
#include "yb/yql/cql/ql/ptree/column_arg.h"


namespace yb {
namespace ql {

class Selectivity {
 public:
  Selectivity(MemoryContext *memctx,
              const client::YBSchema& schema,
              const MCVector<ColumnOp>& key_where_ops,
              const MCList<ColumnOp>& where_ops);

  Selectivity(MemoryContext *memctx,
              const IndexInfo& index_info,
              const MCVector<ColumnOp>& key_where_ops,
              const MCList<ColumnOp>& where_ops,
              const MCSet<int32>& column_refs);

  int prefix_length() const {
    return prefix_length_;
  }

  bool ends_with_range() const {
    return ends_with_range_;
  }

  bool is_index() const {
    return is_index_;
  }

  bool is_local() const {
    return is_local_;
  }

  bool enough_for_read() const {
    return enough_for_read_;
  }

  TableId index_id() const {
    return index_id_;
  }

  bool operator<(const Selectivity& other) const;

 private:

  // Find selectivity, currently defined as length of longest fully specified prefix and whether
  // there is a range operation immediately after the prefix.
  void FindSelectivity(MemoryContext *memctx,
                       const MCVector<ColumnOp>& key_where_ops,
                       const MCList<ColumnOp>& where_ops,
                       int num_hash_key_columns);
  // Properly update ops (for means of prefix and ends with range computations), for operation
  // op at index (in ops) idx.
  void CheckOperator(int idx, const yb::QLOperator& op, MCVector<QLOperator> *ops);


  MCUnorderedMap<int, int> id_to_idx_;  // Mapping from table column id to idx in index.
  int prefix_length_ = 0;               // Length of fully specified prefix in index.
  bool ends_with_range_ = false;        // Whether there is a range clause after prefix.
  bool is_index_ = false;               // Whether this is an index or the indexed table.
  bool is_local_ = false;               // Whether the index is local (for indexed table, this is
                                        // true).
  bool enough_for_read_ = false;        // Whether the index/indexed table is enough for read;
                                        // of course, this is true for indexed table.
  TableId index_id_;                    // Index table id (null for indexed table).
};

}  // namespace ql
}  // namespace yb

#endif  // YB_YQL_CQL_QL_PTREE_SELECTIVITY_H
