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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/cql/ql/ptree/pt_list_roles_permissions.h"

#include "yb/client/table.h"

#include "yb/gutil/strings/substitute.h"

#include "yb/util/enums.h"
#include "yb/util/flags.h"
#include "yb/util/result.h"

#include "yb/yql/cql/ql/ptree/pt_grant_revoke.h"
#include "yb/yql/cql/ql/ptree/pt_option.h"
#include "yb/yql/cql/ql/ptree/sem_context.h"
#include "yb/yql/cql/ql/ptree/sem_state.h"
#include "yb/yql/cql/ql/ptree/yb_location.h"
#include "yb/yql/cql/ql/util/errcodes.h"

DECLARE_bool(use_cassandra_authentication);
// Defined in common_flags.cc, because it also gates DESCRIBE on a single role (master and tserver).
DECLARE_bool(ycql_enable_list_roles_permissions);

namespace yb {
namespace ql {

using std::string;
using strings::Substitute;

namespace {

Status CheckListEnabled(SemContext* sem_context, const TreeNode* node) {
  if (!FLAGS_ycql_enable_list_roles_permissions) {
    return sem_context->Error(node,
        "LIST ROLES and LIST PERMISSIONS are not enabled until the cluster upgrade is finalized",
        ErrorCode::FEATURE_NOT_SUPPORTED);
  }
  return Status::OK();
}

}  // namespace

std::ostream& operator<<(std::ostream& out, const PTListResource& resource) {
  if (!resource.specified) {
    return out << "<all resources>";
  }
  out << ResourceType_Name(resource.type);
  if (resource.name != nullptr) {
    out << " " << resource.name->QLName();
  }
  return out;
}

//--------------------------------------------------------------------------------------------------
// LIST ROLES statement.

PTListRoles::PTListRoles(MemoryContext* memctx,
                         YBLocationPtr loc,
                         const MCSharedPtr<MCString>& role_name,
                         bool recursive)
    : TreeNode(memctx, loc),
      role_name_(role_name),
      recursive_(recursive) {
}

PTListRoles::~PTListRoles() {
}

Status PTListRoles::Analyze(SemContext* sem_context) {
  SemState sem_state(sem_context);
  // Like the other role statements, LIST requires authentication. With
  // use_cassandra_authentication=false this returns the same error as Apache Cassandra with
  // AllowAllAuthenticator.
  RETURN_NOT_AUTH_ENABLED(sem_context);
  RETURN_NOT_OK(CheckListEnabled(sem_context, this));

  // Whether the OF role exists, and whether the caller may see it, is checked by the executor
  // once it has read the role catalog.
  PrintSemanticAnalysisResult(sem_context);
  return Status::OK();
}

void PTListRoles::PrintSemanticAnalysisResult(SemContext* sem_context) {
  MCString sem_output("\tLIST ROLES", sem_context->PTempMem());
  if (has_role_name()) {
    sem_output = sem_output + " OF " + role_name_->c_str();
  }
  if (!recursive_) {
    sem_output = sem_output + " NORECURSIVE";
  }
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << sem_output;
}

//--------------------------------------------------------------------------------------------------
// LIST PERMISSIONS statement.

PTListPermissions::PTListPermissions(MemoryContext* memctx,
                                     YBLocationPtr loc,
                                     const MCSharedPtr<MCString>& permission_name,
                                     const PTListResource& resource,
                                     const MCSharedPtr<MCString>& role_name,
                                     bool recursive)
    : TreeNode(memctx, loc),
      permission_name_(permission_name),
      resource_(resource),
      role_name_(role_name),
      recursive_(recursive) {
}

PTListPermissions::~PTListPermissions() {
}

Status PTListPermissions::Analyze(SemContext* sem_context) {
  SemState sem_state(sem_context);
  RETURN_NOT_AUTH_ENABLED(sem_context);
  RETURN_NOT_OK(CheckListEnabled(sem_context, this));

  const auto& permission_map = PTGrantRevokePermission::kPermissionMap;
  auto iterator = permission_map.find(string(permission_name_->c_str()));
  if (iterator == permission_map.end()) {
    return sem_context->Error(this, Substitute("Unknown Permission '$0'",
                                               permission_name_->c_str()).c_str(),
                              ErrorCode::SYNTAX_ERROR);
  }
  permission_ = iterator->second;

  // Unlike GRANT, LIST does not check that the permission applies to the resource type: in
  // Apache Cassandra "LIST DESCRIBE ON ROLE r" is valid and returns DESCRIBE granted on the
  // parent <all roles>.

  // The resource must exist (Cassandra checks this before authorization). Role existence is
  // checked by the executor together with the OF role, from the same catalog read.
  if (resource_.specified) {
    switch (resource_.type) {
      case ResourceType::KEYSPACE: {
        RETURN_NOT_OK(resource_.name->AnalyzeName(sem_context, ObjectType::SCHEMA));
        const string keyspace = resource_.name->last_name().c_str();
        auto exists = sem_context->KeyspaceExists(keyspace);
        if (!exists.ok()) {
          return sem_context->Error(this, exists.status(), ErrorCode::SERVER_ERROR);
        }
        if (!*exists) {
          return sem_context->Error(this,
                                    Substitute("<keyspace $0> doesn't exist", keyspace).c_str(),
                                    ErrorCode::KEYSPACE_NOT_FOUND);
        }
        break;
      }
      case ResourceType::TABLE: {
        // Resolves an unqualified table name against the current keyspace.
        RETURN_NOT_OK(resource_.name->AnalyzeName(sem_context, ObjectType::TABLE));
        const client::YBTableName table_name = resource_.name->ToTableName();
        auto table = sem_context->GetTableDesc(table_name);
        // Same notion of "table" as SemContext::LookupTable: indexes and non-CQL tables are not
        // resources that permissions can be granted on.
        if (table == nullptr || table->IsIndex() ||
            table->table_type() != client::YBTableType::YQL_TABLE_TYPE) {
          return sem_context->Error(this,
                                    Substitute("<table $0.$1> doesn't exist",
                                               table_name.namespace_name(),
                                               table_name.table_name()).c_str(),
                                    ErrorCode::OBJECT_NOT_FOUND);
        }
        break;
      }
      case ResourceType::ROLE:
        RETURN_NOT_OK(resource_.name->AnalyzeName(sem_context, ObjectType::ROLE));
        break;
      case ResourceType::ALL_KEYSPACES: FALLTHROUGH_INTENDED;
      case ResourceType::ALL_ROLES:
        break;
    }
  }

  PrintSemanticAnalysisResult(sem_context);
  return Status::OK();
}

string PTListPermissions::canonical_resource() const {
  DCHECK(resource_.specified);
  switch (resource_.type) {
    case ResourceType::ALL_KEYSPACES:
      return kRolesDataResource;
    case ResourceType::KEYSPACE:
      return get_canonical_keyspace(resource_.name->last_name().c_str());
    case ResourceType::TABLE:
      return get_canonical_table(resource_.name->first_name().c_str(),
                                 resource_.name->last_name().c_str());
    case ResourceType::ALL_ROLES:
      return kRolesRoleResource;
    case ResourceType::ROLE:
      return get_canonical_role(resource_.name->last_name().c_str());
  }
  FATAL_INVALID_ENUM_VALUE(ResourceType, resource_.type);
}

string PTListPermissions::keyspace_name() const {
  if (!resource_.specified) {
    return string();
  }
  switch (resource_.type) {
    case ResourceType::KEYSPACE:
      return resource_.name->last_name().c_str();
    case ResourceType::TABLE:
      return resource_.name->first_name().c_str();
    case ResourceType::ALL_KEYSPACES:
    case ResourceType::ALL_ROLES:
    case ResourceType::ROLE:
      return string();
  }
  FATAL_INVALID_ENUM_VALUE(ResourceType, resource_.type);
}

void PTListPermissions::PrintSemanticAnalysisResult(SemContext* sem_context) {
  MCString sem_output("\tLIST PERMISSIONS", sem_context->PTempMem());
  sem_output = sem_output + " Permission : " + permission_name_->c_str();
  if (has_resource()) {
    sem_output = sem_output + " Resource : " + canonical_resource().c_str();
  }
  if (has_role_name()) {
    sem_output = sem_output + " Role : " + role_name_->c_str();
  }
  if (!recursive_) {
    sem_output = sem_output + " NORECURSIVE";
  }
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << sem_output;
}

}  // namespace ql
}  // namespace yb
