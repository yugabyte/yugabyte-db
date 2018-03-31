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
// Structure definitions for column descriptor of a table.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_PGSQL_PTREE_COLUMN_DESC_H_
#define YB_YQL_PGSQL_PTREE_COLUMN_DESC_H_

#include "yb/common/types.h"
#include "yb/yql/pgsql/ptree/pg_ttype.h"
#include "yb/util/memory/mc_types.h"

namespace yb {
namespace pgsql {

//--------------------------------------------------------------------------------------------------

// This class can be used to describe any reference of a column.
class ColumnDesc {
 public:
  static const int kHiddenColumnCount = 2;

  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::shared_ptr<ColumnDesc> SharedPtr;
  typedef std::shared_ptr<const ColumnDesc> SharedPtrConst;

  ColumnDesc()
      : index_(-1),
        id_(-1),
        is_hash_(false),
        is_primary_(false),
        ql_type_(QLType::Create(DataType::UNKNOWN_DATA)),
        internal_type_(InternalType::VALUE_NOT_SET) {
  }

  void Init(int index,
            int id,
            string name,
            bool is_hash,
            bool is_primary,
            int32_t order,
            const std::shared_ptr<QLType>& ql_type,
            InternalType internal_type) {
    index_ = index,
    id_ = id;
    name_ = name;
    is_hash_ = is_hash;
    is_primary_ = is_primary;
    order_ = order;
    ql_type_ = ql_type;
    internal_type_ = internal_type;
  }

  bool IsInitialized() const {
    return (index_ >= 0);
  }

  int index() const {
    return index_;
  }

  int id() const {
    return id_;
  }

  const string& name() const {
    return name_;
  }

  bool is_hash() const {
    return is_hash_;
  }

  bool is_primary() const {
    return is_primary_;
  }

  int32_t order() const {
    return order_;
  }

  std::shared_ptr<QLType> ql_type() const {
    return ql_type_;
  }

  InternalType internal_type() const {
    return internal_type_;
  }

 private:
  int index_;
  int id_;
  string name_;
  bool is_hash_;
  bool is_primary_;
  int32_t order_;
  std::shared_ptr<QLType> ql_type_;
  InternalType internal_type_;
};

}  // namespace pgsql
}  // namespace yb

#endif  // YB_YQL_PGSQL_PTREE_COLUMN_DESC_H_
