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
// Treenode definitions for ALTER TYPE statements.
//--------------------------------------------------------------------------------------------------

#include "yb/yql/cql/ql/ptree/pt_alter_role.h"

#include "yb/util/crypt.h"

#include "yb/yql/cql/ql/ptree/sem_context.h"
#include "yb/yql/cql/ql/ptree/sem_state.h"
#include "yb/yql/cql/ql/ptree/yb_location.h"

DECLARE_bool(use_cassandra_authentication);
DEFINE_RUNTIME_bool(ycql_allow_non_authenticated_password_reset, false,
    "If to allow non-authenticated user to reset the password.");

namespace yb {
namespace ql {

using strings::Substitute;
using yb::util::bcrypt_hashpw;
using yb::util::kBcryptHashSize;

//--------------------------------------------------------------------------------------------------
// Alter Role.

PTAlterRole::PTAlterRole(MemoryContext* memctx,
                         YBLocation::SharedPtr loc,
                         const MCSharedPtr<MCString>& name,
                         const PTRoleOptionListNode::SharedPtr& roleOptions)
    : TreeNode(memctx, loc),
      name_(name),
      roleOptions_(roleOptions) {
}

PTAlterRole::~PTAlterRole() {
}

Status PTAlterRole::Analyze(SemContext* sem_context) {
  SemState sem_state(sem_context);

  if (!FLAGS_ycql_allow_non_authenticated_password_reset) {
    RETURN_NOT_AUTH_ENABLED(sem_context);
    RETURN_NOT_OK(
        sem_context->CheckHasRolePermission(loc(), PermissionType::ALTER_PERMISSION, role_name()));
  }

  // Save context state, and set "this" as current column in the context.
  SymbolEntry cached_entry = *sem_context->current_processing_id();
  if (roleOptions_!= nullptr) {
    RETURN_NOT_OK(roleOptions_->Analyze(sem_context));

    bool seen_password = false;
    bool seen_superuser = false;
    bool seen_login = false;

    for (auto& roleOption : roleOptions_->node_list()) {
      switch (roleOption->option_type()) {
        case PTRoleOptionType::kLogin : {
          if (seen_login) {
            return sem_context->Error(roleOption, ErrorCode::INVALID_ROLE_DEFINITION);
          }
          PTRoleLogin *loginOpt = static_cast<PTRoleLogin*>(roleOption.get());
          login_ = loginOpt->login();
          seen_login = true;
          break;
        }
        case PTRoleOptionType::kPassword : {
          if (seen_password) {
            return sem_context->Error(roleOption, ErrorCode::INVALID_ROLE_DEFINITION);
          }
          PTRolePassword *passwordOpt = static_cast<PTRolePassword*>(roleOption.get());

          char hash[kBcryptHashSize];
          int ret = bcrypt_hashpw(passwordOpt->password(), hash);
          if (ret != 0) {
            return STATUS(IllegalState, Substitute("Could not hash password, reason: $0", ret));
          }
          salted_hash_ = MCMakeShared<MCString>(sem_context->PSemMem(), hash , kBcryptHashSize);
          seen_password = true;
          break;
        }
        case PTRoleOptionType::kSuperuser: {
          if (seen_superuser) {
            return sem_context->Error(roleOption, ErrorCode::INVALID_ROLE_DEFINITION);
          }
          PTRoleSuperuser *superuserOpt = static_cast<PTRoleSuperuser*>(roleOption.get());
          superuser_ = superuserOpt->superuser();
          seen_superuser = true;
          break;
        }
      }
    }

  }

  // Restore the context value as we are done with this table.
  sem_context->set_current_processing_id(cached_entry);
  if (VLOG_IS_ON(3)) {
    PrintSemanticAnalysisResult(sem_context);
  }

  return Status::OK();
}

void PTAlterRole::PrintSemanticAnalysisResult(SemContext* sem_context) {
  MCString sem_output("\tAlter Role ", sem_context->PTempMem());
  sem_output = sem_output + " role_name  " + role_name() + " salted_hash = " + *salted_hash_;
  sem_output = sem_output + " login = " + (login() ? "true" : "false");
  sem_output = sem_output + " superuser = " + (superuser() ? "true" : "false");
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << sem_output;
}

}  // namespace ql
}  // namespace yb
