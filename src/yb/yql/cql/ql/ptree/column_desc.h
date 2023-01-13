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

#include "yb/client/schema.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------

// This class can be used to describe any reference of a column.
class ColumnDesc {
 public:
  ColumnDesc(const size_t index,
             const int id,
             const std::string& name,
             const bool is_hash,
             const bool is_primary,
             const bool is_static,
             const bool is_counter,
             const std::shared_ptr<QLType>& ql_type,
             const InternalType internal_type,
             bool has_mangled_name = false)
      : index_(index),
        id_(id),
        name_(name),
        is_hash_(is_hash),
        is_primary_(is_primary),
        is_static_(is_static),
        is_counter_(is_counter),
        ql_type_(ql_type),
        internal_type_(internal_type),
        has_mangled_name_(has_mangled_name) {
  }

  size_t index() const {
    return index_;
  }

  int id() const {
    return id_;
  }

  // User name (not mangled).
  std::string name() const;

  // Index column name (mangled).
  std::string MangledName() const;

  // Return the name that is kept in catalog.
  // - For Catalog::Table, user-defined-column name is not mangled.
  // - For Catalong::IndexTable, expression-column name (ColumnRef, JsonRef, ...) is mangled.
  std::string MetadataName() const {
    return name_;
  }

  bool is_hash() const {
    return is_hash_;
  }

  bool is_primary() const {
    return is_primary_;
  }

  bool is_static() const {
    return is_static_;
  }

  bool is_counter() const {
    return is_counter_;
  }

  const std::shared_ptr<QLType>& ql_type() const {
    return ql_type_;
  }

  InternalType internal_type() const {
    return internal_type_;
  }

  void set_id(int id) {
    id_ = id;
  }

 private:
  size_t index_;
  int id_ = -1;
  std::string name_;
  bool is_hash_ = false;
  bool is_primary_ = false;
  bool is_static_ = false;
  bool is_counter_ = false;
  std::shared_ptr<QLType> ql_type_;
  InternalType internal_type_ = InternalType::VALUE_NOT_SET;
  // Depending on whether the table object is user-table, old index-table, or new index-table, the
  // column name might or might not be mangled. The member "has_mangled_name_" is added so that we
  // don't have to concern about which object this column belongs to.
  bool has_mangled_name_;
};

}  // namespace ql
}  // namespace yb
