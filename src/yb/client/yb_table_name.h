// Copyright (c) YugaByte, Inc.

#ifndef YB_CLIENT_YB_TABLE_NAME_H_
#define YB_CLIENT_YB_TABLE_NAME_H_

#include <string>

#ifdef YB_HEADERS_NO_STUBS
#include "yb/util/logging.h"
#else
#include "yb/client/stubs.h"
#endif


namespace yb {

namespace master {
class TableIdentifierPB;
}

namespace client {

// Is system keyspace read-only?
DECLARE_bool(yb_system_namespace_readonly);

// The class is used to store a table name, which can include namespace name as a suffix.
class YBTableName {
 public:
  // Empty (undefined) name.
  YBTableName() {}

  // Complex table name: 'namespace_name.table_name'.
  // The namespace must not be empty.
  // For the case of undefined namespace the next constructor must be used.
  YBTableName(const std::string& namespace_name, const std::string& table_name) {
    set_namespace_name(namespace_name);
    set_table_name(table_name);
  }

  // Simple table name (no namespace provided at the moment of construction).
  // In this case the namespace has not been set yet and it MUST be set later.
  explicit YBTableName(const std::string& table_name) {
    set_table_name(table_name);
  }

  // Copy constructor.
  YBTableName(const YBTableName& name) {
    *this = name;
  }

  // Move constructor.
  YBTableName(YBTableName&& name) : namespace_name_(std::move(name.namespace_name_)),
      table_name_(std::move(name.table_name_)) {}

  bool has_namespace() const {
    return !namespace_name_.empty();
  }

  const std::string& namespace_name() const {
    return namespace_name_; // Can be empty.
  }

  const std::string& resolved_namespace_name() const {
    DCHECK(has_namespace()); // At the moment the namespace name must NEVER be empty.
                             // It must be set by set_namespace_name() before this call.
                             // If the check fails - you forgot to call set_namespace_name().
    return namespace_name_;
  }

  const std::string& table_name() const {
    return table_name_;
  }

  bool is_system() const {
    return IsSystemNamespace(resolved_namespace_name());
  }

  std::string ToString() const {
    return (has_namespace() ? namespace_name_ + '.' + table_name_ : table_name_);
  }

  void set_namespace_name(const std::string& namespace_name) {
    DCHECK(!namespace_name.empty());
    namespace_name_ = namespace_name;
  }

  void set_table_name(const std::string& table_name) {
    DCHECK(!table_name.empty());
    table_name_ = table_name;
  }

  YBTableName& operator =(const YBTableName& name) {
    namespace_name_ = name.namespace_name_;
    table_name_ = name.table_name_;
    return *this;
  }

  // ProtoBuf helpers.
  void SetIntoTableIdentifierPB(master::TableIdentifierPB* id) const;

  static bool IsSystemNamespace(const std::string& namespace_name);

 private:
  std::string namespace_name_; // Can be empty, that means the namespace has not been set yet.
  std::string table_name_;
};

inline bool operator ==(const YBTableName& lhs, const YBTableName& rhs) {
  return (lhs.namespace_name() == rhs.namespace_name() && lhs.table_name() == rhs.table_name());
}

inline bool operator !=(const YBTableName& lhs, const YBTableName& rhs) {
  return !(lhs == rhs);
}

// In order to be able to use YBTableName with boost::hash
size_t hash_value(const YBTableName& table_name);

}  // namespace client
}  // namespace yb

#endif  // YB_CLIENT_YB_TABLE_NAME_H_
