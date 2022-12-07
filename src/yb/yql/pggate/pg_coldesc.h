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

#pragma once


namespace yb {
namespace pggate {

//--------------------------------------------------------------------------------------------------

// This class can be used to describe any reference of a column.
class ColumnDesc {
 public:
  static const int kHiddenColumnCount = 2;

  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::shared_ptr<ColumnDesc> SharedPtr;
  typedef std::shared_ptr<const ColumnDesc> SharedPtrConst;

  ColumnDesc() : ql_type_(QLType::Create(DataType::UNKNOWN_DATA)) {
  }

  void Init(int index,
            int id,
            string name,
            bool is_partition,
            bool is_primary,
            int32_t attr_num,
            const std::shared_ptr<QLType>& ql_type,
            InternalType internal_type,
            SortingType sorting_type) {
    index_ = index,
    id_ = id;
    name_ = name;
    is_partition_ = is_partition;
    is_primary_ = is_primary;
    attr_num_ = attr_num;
    ql_type_ = ql_type;
    internal_type_ = internal_type;
    sorting_type_ = sorting_type;
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

  bool is_partition() const {
    return is_partition_;
  }

  bool is_primary() const {
    return is_primary_;
  }

  int32_t attr_num() const {
    return attr_num_;
  }

  std::shared_ptr<QLType> ql_type() const {
    return ql_type_;
  }

  InternalType internal_type() const {
    return internal_type_;
  }

  SortingType sorting_type() const {
    return sorting_type_;
  }

 private:
  int index_ = -1;
  int id_ = -1;
  string name_;
  bool is_partition_ = false;
  bool is_primary_ = false;
  int32_t attr_num_ = -1;
  std::shared_ptr<QLType> ql_type_;
  InternalType internal_type_ = InternalType::VALUE_NOT_SET;
  SortingType sorting_type_ = SortingType::kNotSpecified;
};

}  // namespace pggate
}  // namespace yb
