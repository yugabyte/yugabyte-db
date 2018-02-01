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

#include "yb/yql/cql/ql/ptree/selectivity.h"
#include "yb/yql/cql/ql/ptree/sem_context.h"

//--------------------------------------------------------------------------------------------------

namespace yb {
namespace ql {

namespace {

// Return whether the index is enough to perform the read on its own.
bool EnoughForRead(MemoryContext *memctx,
                   const IndexInfo &index_info,
                   const MCSet<int32> column_refs) {
  MCUnorderedSet<int> indexed_column_ids(memctx);
  for (const IndexInfo::IndexColumn &index_col : index_info.columns()) {
    indexed_column_ids.insert(index_col.indexed_column_id);
  }

  for (int table_col_id : column_refs) {
    if (indexed_column_ids.find(table_col_id) == indexed_column_ids.end()) {
      return false;
    }
  }

  return true;
}

} // namespace

Selectivity::Selectivity(MemoryContext *memctx,
                         const client::YBSchema& schema,
                         const MCVector<ColumnOp>& key_where_ops,
                         const MCList<ColumnOp>& where_ops)
    : id_to_idx_(memctx),
      is_index_(false),
      is_local_(true),
      enough_for_read_(true) {
  for (size_t i = 0; i < schema.num_key_columns(); i++) {
    id_to_idx_[schema.ColumnId(i)] = i;
  }

  FindSelectivity(memctx, key_where_ops, where_ops, schema.num_hash_key_columns());
}

Selectivity::Selectivity(MemoryContext *memctx,
                         const IndexInfo& index_info,
                         const MCVector<ColumnOp>& key_where_ops,
                         const MCList<ColumnOp>& where_ops,
                         const MCSet<int32>& column_refs)
    : id_to_idx_(memctx),
      is_index_(true),
      is_local_(index_info.is_local()),
      index_id_(index_info.table_id()) {
  for (int i = 0; i < index_info.key_column_count(); i++) {
    id_to_idx_[index_info.column(i).indexed_column_id] = i;
  }

  FindSelectivity(memctx, key_where_ops, where_ops, index_info.hash_column_count());

  // If prefix length is 0, the table is going to be better than the index anyway (because prefix
  // length 0 means do full index scan), so don't even check whether the index is enough for the
  // read.
  if (prefix_length_ != 0) {
    enough_for_read_ = EnoughForRead(memctx, index_info, column_refs);
  } else {
    enough_for_read_ = false;
  }
}

void Selectivity::FindSelectivity(MemoryContext *memctx,
                                  const MCVector<ColumnOp>& key_where_ops,
                                  const MCList<ColumnOp>& where_ops,
                                  const int num_hash_key_columns) {
  // The operation on each column, ordered how the columns are ordered in the
  // table or index we analyze.
  MCVector<QLOperator> ops(memctx);
  ops.reserve(id_to_idx_.size());
  for (int i = 0; i < id_to_idx_.size(); i++) {
    ops.push_back(QL_OP_NOOP);
  }

  for (const ColumnOp& col_op : where_ops) {
    const MCUnorderedMap<int, int>::const_iterator iter = id_to_idx_.find(col_op.desc()->id());
    if (iter != id_to_idx_.end()) {
      CheckOperator(iter->second, col_op.yb_op(), &ops);
    }
  }

  for (const ColumnOp& col_op : key_where_ops) {
    const MCUnorderedMap<int, int>::const_iterator iter = id_to_idx_.find(col_op.desc()->id());
    if (iter != id_to_idx_.end()) {
      CheckOperator(iter->second, col_op.yb_op(), &ops);
    }
  }

  // First check whether hash keys are fully specified.
  while (prefix_length_ < num_hash_key_columns &&
         (ops[prefix_length_] == QL_OP_EQUAL ||
          ops[prefix_length_] == QL_OP_IN)) {
    prefix_length_++;
  }

  // If hash key not fully specified, set prefix length to 0, as we will have to do a full table
  // scan anyway.
  if (prefix_length_ != num_hash_key_columns) {
    prefix_length_ = 0;
    ends_with_range_ = false;
    return;
  }

  // Now find the prefix length.
  while (prefix_length_ < ops.size() && ops[prefix_length_] == QL_OP_EQUAL) {
    prefix_length_++;
  }

  ends_with_range_ = (prefix_length_ < ops.size()) && ops[prefix_length_] == QL_OP_GREATER_THAN;
}

void Selectivity::CheckOperator(int idx, const yb::QLOperator& op, MCVector<QLOperator> *ops) {
  switch (op) {
    case QL_OP_GREATER_THAN: FALLTHROUGH_INTENDED;
    case QL_OP_GREATER_THAN_EQUAL: FALLTHROUGH_INTENDED;
    case QL_OP_LESS_THAN: FALLTHROUGH_INTENDED;
    case QL_OP_LESS_THAN_EQUAL: {
      ops->at(idx) = QL_OP_GREATER_THAN;
      break;
    }
    case QL_OP_EQUAL: {
      ops->at(idx) = QL_OP_EQUAL;
      break;
    }
    case QL_OP_IN: {
      ops->at(idx) = QL_OP_IN;
      break;
    }
    case QL_OP_NOOP: FALLTHROUGH_INTENDED;
    case QL_OP_NOT_IN: {
      break;
    }
    default: {
      // Should have been caught beforehand (as this is called in pt_select after analyzing
      // the where clause).
      break;
    }
  }
}

bool Selectivity::operator<(const Selectivity& other) const {
  // If different prefix lengths, the shorter oen is worse.
  if (prefix_length_ != other.prefix_length_) {
    return prefix_length_ < other.prefix_length_;
  }

  // If same prefix lengths, but one ends with a range query and the other does not, the one that
  // does not is worse.
  if (ends_with_range_ != other.ends_with_range_) {
    return ends_with_range_ < other.ends_with_range_;
  }

  // If both previous values are the same, the index is worse than the indexes table.
  if (is_index_ != other.is_index_) {
    return is_index_ > other.is_index_;
  }

  // An index that can't perform the read on its own is worse than one that can.
  if (enough_for_read_ != other.enough_for_read_) {
    return enough_for_read_ < other.enough_for_read_;
  }

  // If all previous values are the same, a non-local index is worse than a local index.
  return is_local_ < other.is_local_;
}

}  // namespace ql
}  // namespace yb
