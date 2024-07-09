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

#include <string>
#include <boost/optional.hpp>

#include "yb/util/flags.h"
#include "yb/util/memory/memory_usage.h"

#include "yb/common/common_types.pb.h"

#include "yb/master/master_types.pb.h"

namespace yb {

namespace master {
class NamespaceIdentifierPB;
class TableIdentifierPB;
}

namespace client {

// Is system keyspace read-only?
DECLARE_bool(yb_system_namespace_readonly);

// The class is used to store a table name, which can include namespace name as a suffix.
class YBTableName {
 public:
  // Empty (undefined) name. Only the table type can be known.
  explicit YBTableName(YQLDatabase db_type = YQL_DATABASE_UNKNOWN) : namespace_type_(db_type) {}

  // Complex table name: 'namespace_name.table_name'.
  // The namespace must not be empty.
  // For the case of undefined namespace the next constructor must be used.
  YBTableName(YQLDatabase db_type,
              const std::string& namespace_name,
              const std::string& table_name) : namespace_type_(db_type) {
    set_namespace_name(namespace_name);
    set_table_name(table_name);
  }

  YBTableName(YQLDatabase db_type,
              const std::string& namespace_id,
              const std::string& namespace_name,
              const std::string& table_name) : namespace_type_(db_type) {
    set_namespace_id(namespace_id);
    set_namespace_name(namespace_name);
    set_table_name(table_name);
  }

  YBTableName(YQLDatabase db_type,
              const std::string& namespace_id,
              const std::string& namespace_name,
              const std::string& table_id,
              const std::string& table_name,
              const std::string& pgschema_name = "",
              boost::optional<master::RelationType> relation_type = boost::none)
            : namespace_type_(db_type) {
    set_namespace_id(namespace_id);
    set_namespace_name(namespace_name);
    set_table_id(table_id);
    set_table_name(table_name);
    set_pgschema_name(pgschema_name);
    set_relation_type(relation_type);
  }

  // Simple table name (no namespace provided at the moment of construction).
  // In this case the namespace has not been set yet and it MUST be set later.
  YBTableName(YQLDatabase db_type, const std::string& table_name) : namespace_type_(db_type) {
    set_table_name(table_name);
  }

  bool empty() const {
    return namespace_id_.empty() && namespace_name_.empty() && table_name_.empty();
  }

  bool has_namespace() const {
    return !namespace_name_.empty();
  }

  const std::string& namespace_name() const {
    return namespace_name_; // Can be empty.
  }

  const std::string& namespace_id() const {
    return namespace_id_; // Can be empty.
  }

  YQLDatabase namespace_type() const {
    return namespace_type_;
  }

  const std::string& resolved_namespace_name() const;

  bool has_table() const {
    return !table_name_.empty();
  }

  const std::string& table_name() const {
    return table_name_;
  }

  bool has_table_id() const {
    return !table_id_.empty();
  }

  const std::string& table_id() const {
    return table_id_; // Can be empty
  }

  bool has_pgschema_name() const {
    return !pgschema_name_.empty();
  }

  const std::string& pgschema_name() const {
    return pgschema_name_; // Can be empty
  }

  boost::optional<master::RelationType> relation_type() const {
    return relation_type_;
  }

  bool is_system() const;

  bool is_cql_namespace() const {
    return namespace_type_ == YQL_DATABASE_CQL;
  }

  bool is_pgsql_namespace() const {
    return namespace_type_ == YQL_DATABASE_PGSQL;
  }

  bool is_redis_namespace() const {
    return namespace_type_ == YQL_DATABASE_REDIS;
  }

  bool is_redis_table() const;

  std::string ToString() const {
    return has_namespace() ? (namespace_name_ + '.' + table_name_) : table_name_;
  }

  void set_namespace_id(const std::string& namespace_id);
  void set_namespace_name(const std::string& namespace_name);
  void set_table_name(const std::string& table_name);
  void set_table_id(const std::string& table_id);
  void set_pgschema_name(const std::string& pgschema_name);

  void set_relation_type(boost::optional<master::RelationType> relation_type) {
    relation_type_ = relation_type;
  }

  // ProtoBuf helpers.
  void SetIntoTableIdentifierPB(master::TableIdentifierPB* id) const;
  void GetFromTableIdentifierPB(const master::TableIdentifierPB& id);

  void SetIntoNamespaceIdentifierPB(master::NamespaceIdentifierPB* id) const;
  void GetFromNamespaceIdentifierPB(const master::NamespaceIdentifierPB& id);

  size_t DynamicMemoryUsage() const {
    return sizeof(*this) +
           DynamicMemoryUsageOf(
               namespace_id_, namespace_name_, table_id_, table_name_ + pgschema_name_);
  }

 private:
  void check_db_type();

  std::string namespace_id_; // Optional. Can be set when the client knows the namespace id.
  std::string namespace_name_; // Can be empty, that means the namespace has not been set yet.
  YQLDatabase namespace_type_; // Can be empty, that means the namespace id will be used.
  std::string table_id_; // Optional. Can be set when client knows the table id also.
  std::string table_name_;
  std::string pgschema_name_; // Can be empty
  // Optional. Can be set when the client knows the table type.
  boost::optional<master::RelationType> relation_type_;
};

inline bool operator ==(const YBTableName& lhs, const YBTableName& rhs) {
  // Not comparing namespace_id and table_id because they are optional.
  return (lhs.namespace_name() == rhs.namespace_name() && lhs.table_name() == rhs.table_name());
}

inline bool operator !=(const YBTableName& lhs, const YBTableName& rhs) {
  return !(lhs == rhs);
}

// In order to be able to use YBTableName with boost::hash
size_t hash_value(const YBTableName& table_name);

}  // namespace client
}  // namespace yb
