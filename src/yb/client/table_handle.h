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

#ifndef YB_CLIENT_TABLE_HANDLE_H
#define YB_CLIENT_TABLE_HANDLE_H

#include <unordered_map>

#include <boost/optional.hpp>

#include "yb/client/client.h"

#include "yb/common/schema.h"
#include "yb/common/ql_protocol.pb.h"
#include "yb/common/ql_rowblock.h"

#include "yb/util/strongly_typed_bool.h"

namespace yb {

class QLType;
class QLRow;
class QLRowBlock;

namespace client {

class YBTableName;
class YBClient;
class YBqlReadOp;
class YBqlWriteOp;
class YBSchema;
class YBSchemaBuilder;

class TableIterator;
class TableRange;

// Utility class for manually filling QL operations.
class TableHandle {
 public:
  CHECKED_STATUS Create(const YBTableName& table_name,
                        int num_tablets,
                        YBClient* client,
                        YBSchemaBuilder* builder,
                        int num_replicas = 3);

  CHECKED_STATUS Create(const YBTableName& table_name,
                        int num_tablets,
                        const YBSchema& schema,
                        YBClient* client,
                        int num_replicas = 3);

  CHECKED_STATUS Open(const YBTableName& table_name, YBClient* client);

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

  int32_t ColumnId(const std::string &column_name) const {
    auto it = column_ids_.find(column_name);
    return it != column_ids_.end() ? it->second : -1;
  }

  const std::shared_ptr<QLType>& ColumnType(const std::string &column_name) const {
    static std::shared_ptr<QLType> not_found;
    auto it = column_types_.find(yb::ColumnId(ColumnId(column_name)));
    return it != column_types_.end() ? it->second : not_found;
  }

  void AddInt32ColumnValue(
      QLWriteRequestPB* req, const std::string &column_name, int32_t value) const;
  void AddInt64ColumnValue(
      QLWriteRequestPB* req, const std::string &column_name, int64_t value) const;
  void AddStringColumnValue(
      QLWriteRequestPB* req, const std::string &column_name, const std::string &value) const;

  template <class RequestPB>
  void AddInt32HashValue(RequestPB* req, int32_t value) const {
    SetInt32Expression(req->add_hashed_column_values(), value);
  }

  template <class RequestPB>
  void AddInt64HashValue(RequestPB* req, int64_t value) const {
    SetInt64Expression(req->add_hashed_column_values(), value);
  }

  template <class RequestPB>
  void AddStringHashValue(RequestPB* req, const std::string& value) const {
    SetStringExpression(req->add_hashed_column_values(), value);
  }

  void AddInt32RangeValue(QLWriteRequestPB* req, int32_t value) const;
  void AddInt64RangeValue(QLWriteRequestPB* req, int64_t value) const;
  void AddStringRangeValue(QLWriteRequestPB* req, const std::string& value) const;

  void SetInt32Expression(QLExpressionPB *expr, int32_t value) const;
  void SetInt64Expression(QLExpressionPB *expr, int64_t value) const;
  void SetStringExpression(QLExpressionPB *expr, const std::string &value) const;

  // Set a column id without value - for DELETE
  void SetColumn(QLColumnValuePB *column_value, const std::string &column_name) const;

  // Set a int32 column value comparison.
  // E.g. <column-id> = <int32-value>
  void SetInt32Condition(
      QLConditionPB *const condition, const std::string &column_name, const QLOperator op,
      const int32_t value) const;

  // Set a string column value comparison.
  // E.g. <column-id> = <string-value>
  void SetStringCondition(
      QLConditionPB *const condition, const std::string &column_name, const QLOperator op,
      const std::string &value) const;

  // Add a int32 column value comparison under a logical comparison condition.
  // E.g. Add <column-id> = <int32-value> under "... AND <column-id> = <int32-value>".
  void AddInt32Condition(
      QLConditionPB *const condition, const std::string &column_name, const QLOperator op,
      const int32_t value) const;

  // Add a string column value comparison under a logical comparison condition.
  // E.g. Add <column-id> = <string-value> under "... AND <column-id> = <string-value>".
  void AddStringCondition(
      QLConditionPB *const condition, const std::string &column_name, const QLOperator op,
      const std::string &value) const;

  // Add a simple comparison operation under a logical comparison condition.
  // E.g. Add <EXISTS> under "... AND <EXISTS>".
  void AddCondition(QLConditionPB *const condition, const QLOperator op) const;

  void AddColumns(const std::vector<std::string>& columns, QLReadRequestPB* req) const;

  const YBTablePtr& table() const {
    return table_;
  }

  const YBTableName& name() const {
    return table_->name();
  }

  const YBSchema& schema() const {
    return table_->schema();
  }

  YBTable* operator->() const {
    return table_.get();
  }

  YBTable* get() const {
    return table_.get();
  }

  std::vector<std::string> AllColumnNames() const {
    std::vector<std::string> result;
    result.reserve(table_->schema().columns().size());
    for (const auto& column : table_->schema().columns()) {
      result.push_back(column.name());
    }
    return result;
  }

 private:
  typedef std::unordered_map<std::string, yb::ColumnId> ColumnIdsMap;
  typedef std::unordered_map<yb::ColumnId, const std::shared_ptr<QLType>> ColumnTypesMap;

  YBTablePtr table_;
  ColumnIdsMap column_ids_;
  ColumnTypesMap column_types_;
};

typedef std::function<void(const TableHandle&, QLConditionPB*)> TableFilter;

class TableIterator : public std::iterator<
    std::forward_iterator_tag, QLRow, ptrdiff_t, const QLRow*, const QLRow&> {
 public:
  TableIterator();
  explicit TableIterator(const TableHandle* table,
                         YBConsistencyLevel consistency,
                         const std::vector<std::string>& columns,
                         const TableFilter& filter);

  bool Equals(const TableIterator& rhs) const;

  TableIterator& operator++();

  TableIterator operator++(int) {
    TableIterator result = *this;
    ++*this;
    return result;
  }

  const QLRow& operator*() const;

 private:
  void Move();

  const TableHandle* table_;
  std::vector<YBqlReadOpPtr> ops_;
  size_t ops_index_;
  boost::optional<QLRowBlock> current_block_;
  size_t row_index_;
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

  TableRange(const TableHandle& table, YBConsistencyLevel consistency,
             std::vector<std::string> columns, TableFilter filter)
       : table_(&table), consistency_(consistency), columns_(std::move(columns)),
         filter_(std::move(filter)) {}

  TableRange(const TableHandle& table, std::vector<std::string> columns, TableFilter filter)
       : TableRange(table, YBConsistencyLevel::STRONG, std::move(columns), std::move(filter)) {}

  TableRange(const TableHandle& table, TableFilter filter)
       : TableRange(table, YBConsistencyLevel::STRONG, table.AllColumnNames(), std::move(filter)) {}

  const_iterator begin() const {
    return TableIterator(table_, consistency_, columns_, filter_);
  }

  const_iterator end() const {
    return TableIterator();
  }

 private:
  const TableHandle* table_;
  YBConsistencyLevel consistency_;
  std::vector<std::string> columns_;
  TableFilter filter_;
};

YB_STRONGLY_TYPED_BOOL(Inclusive);

class FilterBetween {
 public:
  FilterBetween(int32_t lower_bound, Inclusive lower_inclusive,
                int32_t upper_bound, Inclusive upper_inclusive,
                std::string column = "key")
      : lower_bound_(lower_bound), lower_inclusive_(lower_inclusive),
        upper_bound_(upper_bound), upper_inclusive_(upper_inclusive),
        column_(std::move(column)) {}

  void operator()(const TableHandle& table, QLConditionPB* condition) const;
 private:
  int32_t lower_bound_;
  Inclusive lower_inclusive_;
  int32_t upper_bound_;
  Inclusive upper_inclusive_;
  std::string column_;
};

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

} // namespace client
} // namespace yb

#endif // YB_CLIENT_TABLE_HANDLE_H
