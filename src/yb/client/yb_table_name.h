// Copyright (c) YugaByte, Inc.

#ifndef YB_CLIENT_YB_TABLE_NAME_H_
#define YB_CLIENT_YB_TABLE_NAME_H_

#include <string>

#ifdef YB_HEADERS_NO_STUBS
#include "yb/util/logging.h"
#else
#include "yb/client/stubs.h"
#endif
#include "yb/util/yb_export.h"

namespace yb {

namespace master {
class TableIdentifierPB;
}

namespace client {

// Is system keyspace read-only?
DECLARE_bool(yb_system_namespace_readonly);

// The class is used to store a table name, which can include namespace name as a suffix.
class YB_EXPORT YBTableName {
 public:
  enum NamespaceId {
    UNKNOWN_NAMESPACE, // The namespace MUST be set later.
    DEFAULT_NAMESPACE  // Use Master default namespace.
  };

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
  // There are 2 different cases:
  // - Default namespace must be used (set NamespaceId = DEFAULT_NAMESPACE - by default).
  // - Namespace will be set later (set NamespaceId = UNKNOWN_NAMESPACE).
  //   In this case the namespace has not been set yet and it MUST be set later.
  explicit YBTableName(const std::string& table_name, NamespaceId ns = DEFAULT_NAMESPACE) {
    if (ns == DEFAULT_NAMESPACE) {
      set_namespace_name(YBTableName::default_namespace());
    }

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

  bool operator ==(const YBTableName& name) const {
    return (namespace_name_ == name.namespace_name_ && table_name_ == name.table_name_);
  }

  bool operator !=(const YBTableName& name) const {
    return !operator ==(name);
  }

  // ProtoBuf helpers.
  void SetIntoTableIdentifierPB(master::TableIdentifierPB* id) const;

  static const std::string& default_namespace();

  static bool IsSystemNamespace(const std::string& namespace_name);

 private:
  std::string namespace_name_; // Can be empty, that means the namespace has not been set yet.
  std::string table_name_;
};

}  // namespace client
}  // namespace yb

#endif  // YB_CLIENT_YB_TABLE_NAME_H_
