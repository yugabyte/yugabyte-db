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
// Treenode definitions for CREATE KEYSPACE statements.
//--------------------------------------------------------------------------------------------------

#include "yb/yql/cql/ql/ptree/pt_create_keyspace.h"

#include "yb/common/redis_constants_common.h"

#include "yb/yql/cql/ql/ptree/sem_context.h"
#include "yb/yql/cql/ql/ptree/yb_location.h"

DECLARE_bool(use_cassandra_authentication);

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------

PTCreateKeyspace::PTCreateKeyspace(MemoryContext *memctx,
    YBLocation::SharedPtr loc,
    const MCSharedPtr<MCString>& name,
    bool create_if_not_exists,
    const PTKeyspacePropertyListNode::SharedPtr& keyspace_properties)
    : TreeNode(memctx, loc),
      name_(name),
      create_if_not_exists_(create_if_not_exists),
      keyspace_properties_(keyspace_properties) {
}

PTCreateKeyspace::~PTCreateKeyspace() {
}

Status PTCreateKeyspace::Analyze(SemContext *sem_context) {
  if (*name_ == common::kRedisKeyspaceName) {
    return sem_context->Error(loc(),
                              strings::Substitute("$0 is a reserved keyspace name",
                                                  common::kRedisKeyspaceName).c_str(),
                              ErrorCode::INVALID_ARGUMENTS);
  }

  if (FLAGS_use_cassandra_authentication) {
    RETURN_NOT_OK(sem_context->CheckHasAllKeyspacesPermission(loc(),
        PermissionType::CREATE_PERMISSION));
  }

  if (keyspace_properties_ != nullptr) {
    // Process table properties.
    RETURN_NOT_OK(keyspace_properties_->Analyze(sem_context));
  }

  if (VLOG_IS_ON(3)) {
    PrintSemanticAnalysisResult(sem_context);
  }

  return Status::OK();
}

void PTCreateKeyspace::PrintSemanticAnalysisResult(SemContext *sem_context) {
  MCString sem_output("\tKeyspace ", sem_context->PTempMem());
  sem_output += name();
  sem_output += (create_if_not_exists()? " IF NOT EXISTS" : "");
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << loc() << "):\n" << sem_output;
}

}  // namespace ql
}  // namespace yb
