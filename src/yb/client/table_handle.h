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

#pragma once

#include <unordered_map>

#include <boost/functional/hash.hpp>
#include <boost/optional.hpp>

#include "yb/client/client_fwd.h"

#include "yb/common/column_id.h"
#include "yb/common/ql_protocol.pb.h"
#include "yb/common/ql_protocol_util.h"
#include "yb/common/read_hybrid_time.h"

#include "yb/qlexpr/ql_rowblock.h"

#include "yb/util/async_util.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb {
namespace client {

class YBTableName;
class YBClient;
class YBqlReadOp;
class YBqlWriteOp;
class YBSchema;
class YBSchemaBuilder;

class TableIterator;
class TableRange;

#define TABLE_HANDLE_TYPE_DECLARATIONS_IMPL(name, lname, type) \
  void PP_CAT3(Add, name, ColumnValue)( \
      QLWriteRequestPB * req, const std::string& column_name, type value) const; \
\
  void PP_CAT3(Set, name, Condition)( \
      QLConditionPB* const condition, const std::string& column_name, const QLOperator op, \
      type value) const; \
  void PP_CAT3(Add, name, Condition)( \
      QLConditionPB* const condition, const std::string& column_name, const QLOperator op, \
      type value) const;

#define TABLE_HANDLE_TYPE_DECLARATIONS(i, data, entry) TABLE_HANDLE_TYPE_DECLARATIONS_IMPL entry

QLMapValuePB* AddMapColumn(QLWriteRequestPB* req, const int32_t& column_id);

void AddMapEntryToColumn(
    QLMapValuePB* map_value_pb, const std::string& entry_key, const std::string& entry_value);

// Utility class for manually filling QL operations.
class TableHandle {
 public:
  Status Create(const YBTableName& table_name,
                int num_tablets,
                YBClient* client,
                YBSchemaBuilder* builder,
                IndexInfoPB* index_info = nullptr);

  Status Create(const YBTableName& table_name,
                int num_tablets,
                const YBSchema& schema,
                YBClient* client,
                IndexInfoPB* index_info = nullptr);

  Status Open(const YBTableName& table_name, YBClient* client);

  Status Reopen();

  std::shared_ptr<YBqlWriteOp> NewWriteOp(QLWriteRequestPB::QLStmtType type) const;

  std::shared_ptr<YBqlWriteOp> NewInsertOp() const {
    return NewWriteOp(QLWriteRequestPB::QL_STMT_INSERT);
  }

  std::shared_ptr<YBqlWriteOp> NewUpdateOp() const {
    return NewWriteOp(QLWriteRequestPB::QL_STMT_UPDATE);
  }

  std::shared_ptr<YBqlWriteOp> NewDeleteOp() const {
    return NewWriteOp(QLWriteRequestPB::QL_STMT_DELETE);
  }

  std::shared_ptr<YBqlReadOp> NewReadOp() const;

  int32_t ColumnId(const std::string& column_name) const {
    auto it = column_ids_.find(column_name);
    return it != column_ids_.end() ? it->second : -1;
  }

  const std::shared_ptr<QLType>& ColumnType(const std::string& column_name) const {
    static std::shared_ptr<QLType> not_found;
    auto it = column_types_.find(yb::ColumnId(ColumnId(column_name)));
    return it != column_types_.end() ? it->second : not_found;
  }

  BOOST_PP_SEQ_FOR_EACH(TABLE_HANDLE_TYPE_DECLARATIONS, ~, QL_PROTOCOL_TYPES);

  // Set a column id without value - for DELETE
  void SetColumn(QLColumnValuePB* column_value, const std::string& column_name) const;

  // Add a simple comparison operation under a logical comparison condition.
  // E.g. Add <EXISTS> under "... AND <EXISTS>".
  void AddCondition(QLConditionPB* const condition, const QLOperator op) const;

  void AddColumns(const std::vector<std::string>& columns, QLReadRequestPB* req) const;

  const YBTablePtr& table() const { return table_; }

  const YBTableName& name() const;

  const YBSchema& schema() const;

  YBTable* operator->() const { return table_.get(); }

  YBTable* get() const { return table_.get(); }

  YBClient* client() const { return client_; }

  std::vector<std::string> AllColumnNames() const;

  QLValuePB* PrepareColumn(QLWriteRequestPB* req, const std::string& column_name) const;
  QLValuePB* PrepareCondition(
      QLConditionPB* const condition, const std::string& column_name, const QLOperator op) const;

 private:
  typedef std::unordered_map<std::string, yb::ColumnId> ColumnIdsMap;
  using ColumnTypesMap =
      std::unordered_map<yb::ColumnId, const std::shared_ptr<QLType>, boost::hash<yb::ColumnId>>;

  YBClient* client_;
  YBTablePtr table_;
  ColumnIdsMap column_ids_;
  ColumnTypesMap column_types_;
};

typedef std::function<void(const TableHandle&, QLConditionPB*)> TableFilter;

struct TableIteratorOptions {
  TableIteratorOptions();

  YBConsistencyLevel consistency = YBConsistencyLevel::STRONG;
  boost::optional<std::vector<std::string>> columns;
  TableFilter filter;
  ReadHybridTime read_time;
  std::string tablet;
  StatusFunctor error_handler;
  MonoDelta timeout = MonoDelta::FromSeconds(60);
};

class TableIterator : public std::iterator<
    std::forward_iterator_tag, qlexpr::QLRow, ptrdiff_t, const qlexpr::QLRow*,
    const qlexpr::QLRow&> {
 public:
  TableIterator();
  explicit TableIterator(const TableHandle* table, const TableIteratorOptions& options);

  bool Equals(const TableIterator& rhs) const;

  TableIterator& operator++();

  TableIterator operator++(int) {
    TableIterator result = *this;
    ++*this;
    return result;
  }

  const qlexpr::QLRow& operator*() const;

  const qlexpr::QLRow* operator->() const { return &**this; }

 private:
  bool ExecuteOps();
  void Move();
  void HandleError(const Status& status);
  bool IsFlushStatusOkOrHandleErrors(FlushStatus flush_status);

  const TableHandle* table_;
  std::vector<YBqlReadOpPtr> ops_;
  std::vector<std::string> partition_key_ends_;
  size_t executed_ops_ = 0;
  size_t ops_index_ = 0;
  boost::optional<qlexpr::QLRowBlock> current_block_;
  const QLPagingStatePB* paging_state_ = nullptr;
  size_t row_index_;
  YBSessionPtr session_;
  StatusFunctor error_handler_;
};

inline bool operator==(const TableIterator& lhs, const TableIterator& rhs) {
  return lhs.Equals(rhs);
}

inline bool operator!=(const TableIterator& lhs, const TableIterator& rhs) {
  return !lhs.Equals(rhs);
}

class TableRange {
 public:
  typedef TableIterator const_iterator;
  typedef TableIterator iterator;

  explicit TableRange(const TableHandle& table, TableIteratorOptions options = {})
      : table_(&table), options_(std::move(options)) {
    if (!options_.columns) {
      options_.columns = table.AllColumnNames();
    }
  }

  const_iterator begin() const { return TableIterator(table_, options_); }

  const_iterator end() const { return TableIterator(); }

 private:
  const TableHandle* table_;
  TableIteratorOptions options_;
};

YB_STRONGLY_TYPED_BOOL(Inclusive);

template <class T>
class FilterBetweenImpl {
 public:
  FilterBetweenImpl(
      const T& lower_bound, Inclusive lower_inclusive, const T& upper_bound,
      Inclusive upper_inclusive, std::string column = "key")
      : lower_bound_(lower_bound),
        lower_inclusive_(lower_inclusive),
        upper_bound_(upper_bound),
        upper_inclusive_(upper_inclusive),
        column_(std::move(column)) {}

  void operator()(const TableHandle& table, QLConditionPB* condition) const;

 private:
  T lower_bound_;
  Inclusive lower_inclusive_;
  T upper_bound_;
  Inclusive upper_inclusive_;
  std::string column_;
};

template <class T>
FilterBetweenImpl<T> FilterBetween(
    const T& lower_bound, Inclusive lower_inclusive, const T& upper_bound,
    Inclusive upper_inclusive, std::string column = "key") {
  return FilterBetweenImpl<T>(lower_bound, lower_inclusive, upper_bound, upper_inclusive, column);
}

class FilterGreater {
 public:
  FilterGreater(int32_t bound, Inclusive inclusive, std::string column = "key")
      : bound_(bound), inclusive_(inclusive), column_(std::move(column)) {}

  void operator()(const TableHandle& table, QLConditionPB* condition) const;

 private:
  int32_t bound_;
  Inclusive inclusive_;
  std::string column_;
};

class FilterLess {
 public:
  FilterLess(int32_t bound, Inclusive inclusive, std::string column = "key")
      : bound_(bound), inclusive_(inclusive), column_(std::move(column)) {}

  void operator()(const TableHandle& table, QLConditionPB* condition) const;

 private:
  int32_t bound_;
  Inclusive inclusive_;
  std::string column_;
};

template <class T>
class FilterEqualImpl {
 public:
  FilterEqualImpl(const T& t, std::string column) : t_(t), column_(std::move(column)) {}

  void operator()(const TableHandle& table, QLConditionPB* condition) const;

 private:
  T t_;
  std::string column_;
};

template <class T>
FilterEqualImpl<T> FilterEqual(const T& t, std::string column = "key") {
  return FilterEqualImpl<T>(t, std::move(column));
}

} // namespace client
} // namespace yb
