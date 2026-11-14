//--------------------------------------------------------------------------------------------------
// Copyright (c) YugabyteDB, Inc.
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
// Tree node definitions for the LIST ROLES and LIST PERMISSIONS statements.
//
// Grammar (Apache Cassandra 3.11):
//   LIST ROLES [ OF role_name ] [ NORECURSIVE ]
//   LIST permissions [ ON resource ] [ OF role_name ] [ NORECURSIVE ]
//
// NORECURSIVE means different things in the two statements, matching Cassandra:
//   - LIST ROLES: list only the roles granted directly to OF role_name.
//   - LIST PERMISSIONS: do not include the parent resources of the ON resource (for example
//     <keyspace ks> and <all keyspaces> for ON TABLE ks.t). Grants inherited through role
//     membership are always included.
//--------------------------------------------------------------------------------------------------

#pragma once

#include <ostream>
#include <string>

#include "yb/common/roles_permissions.h"

#include "yb/yql/cql/ql/ptree/ptree_fwd.h"
#include "yb/yql/cql/ql/ptree/pt_name.h"
#include "yb/yql/cql/ql/ptree/tree_node.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------
// The optional "ON resource" clause of LIST PERMISSIONS. This is a plain value type so that the
// parser can pass it between grammar rules.
struct PTListResource {
  // False when the statement has no ON clause, which means "all resources".
  bool specified = false;
  ResourceType type = ResourceType::ALL_KEYSPACES;
  // Keyspace name for KEYSPACE, [keyspace.]table for TABLE, role name for ROLE.
  // nullptr for ALL KEYSPACES and ALL ROLES.
  PTQualifiedName::SharedPtr name;
};

// Used by the parser's debug trace (%printer in parser_gram.y).
std::ostream& operator<<(std::ostream& out, const PTListResource& resource);

//--------------------------------------------------------------------------------------------------
// LIST ROLES statement.

class PTListRoles : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTListRoles> SharedPtr;
  typedef MCSharedPtr<const PTListRoles> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTListRoles(MemoryContext* memctx, YBLocationPtr loc,
              const MCSharedPtr<MCString>& role_name,
              bool recursive);
  virtual ~PTListRoles();

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTListRoles;
  }

  // Support for shared_ptr.
  template<typename... TypeArgs>
  inline static PTListRoles::SharedPtr MakeShared(MemoryContext* memctx, TypeArgs&&... args) {
    return MCMakeShared<PTListRoles>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual Status Analyze(SemContext* sem_context) override;

  void PrintSemanticAnalysisResult(SemContext* sem_context);

  // Whether the statement has an OF clause.
  bool has_role_name() const {
    return role_name_ != nullptr;
  }

  // Role in the OF clause. Only valid when has_role_name() is true.
  std::string role_name() const {
    return role_name_->c_str();
  }

  // False when NORECURSIVE is given.
  bool recursive() const {
    return recursive_;
  }

 private:
  // nullptr when the statement has no OF clause.
  const MCSharedPtr<MCString> role_name_;
  const bool recursive_;
};

//--------------------------------------------------------------------------------------------------
// LIST PERMISSIONS statement.

class PTListPermissions : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTListPermissions> SharedPtr;
  typedef MCSharedPtr<const PTListPermissions> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTListPermissions(MemoryContext* memctx, YBLocationPtr loc,
                    const MCSharedPtr<MCString>& permission_name,
                    const PTListResource& resource,
                    const MCSharedPtr<MCString>& role_name,
                    bool recursive);
  virtual ~PTListPermissions();

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTListPermissions;
  }

  // Support for shared_ptr.
  template<typename... TypeArgs>
  inline static PTListPermissions::SharedPtr MakeShared(MemoryContext* memctx,
                                                        TypeArgs&&... args) {
    return MCMakeShared<PTListPermissions>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual Status Analyze(SemContext* sem_context) override;

  void PrintSemanticAnalysisResult(SemContext* sem_context);

  // Requested permission. ALL_PERMISSION means every permission. Valid after Analyze().
  PermissionType permission() const {
    return permission_;
  }

  // Whether the statement has an ON clause. Without one, all resources are listed.
  bool has_resource() const {
    return resource_.specified;
  }

  // Resource type of the ON clause. Only valid when has_resource() is true.
  ResourceType resource_type() const {
    return resource_.type;
  }

  // Canonical name of the ON resource as stored in the catalog, for example "data",
  // "data/ks", "data/ks/t", "roles" or "roles/r". Only valid when has_resource() is true and
  // after Analyze(), which resolves the keyspace of an unqualified table name.
  std::string canonical_resource() const;

  // Keyspace name of a KEYSPACE or TABLE ON resource, and an empty string otherwise. Only
  // valid after Analyze(). Use it rather than splitting canonical_resource(), because a quoted
  // keyspace name may contain '/'.
  std::string keyspace_name() const;

  // Whether the statement has an OF clause. Without one, all roles are listed.
  bool has_role_name() const {
    return role_name_ != nullptr;
  }

  // Role in the OF clause. Only valid when has_role_name() is true.
  std::string role_name() const {
    return role_name_->c_str();
  }

  // False when NORECURSIVE is given.
  bool recursive() const {
    return recursive_;
  }

 private:
  const MCSharedPtr<MCString> permission_name_;
  PermissionType permission_ = PermissionType::ALL_PERMISSION;
  const PTListResource resource_;
  // nullptr when the statement has no OF clause.
  const MCSharedPtr<MCString> role_name_;
  const bool recursive_;
};

}  // namespace ql
}  // namespace yb
