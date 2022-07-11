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
// Treenode definitions for USE KEYSPACE statements.
//--------------------------------------------------------------------------------------------------

#include "yb/yql/cql/ql/ptree/pt_use_keyspace.h"

#include "yb/common/redis_constants_common.h"

#include "yb/util/status.h"

#include "yb/yql/cql/ql/ptree/sem_context.h"
#include "yb/yql/cql/ql/ptree/yb_location.h"
#include "yb/yql/cql/ql/util/errcodes.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------

PTUseKeyspace::PTUseKeyspace(MemoryContext *memctx,
                             YBLocation::SharedPtr loc,
                             const MCSharedPtr<MCString>& name)
    : TreeNode(memctx, loc),
      name_(name) {
}

PTUseKeyspace::~PTUseKeyspace() {
}

Status PTUseKeyspace::Analyze(SemContext *sem_context) {
  if (*name_ == common::kRedisKeyspaceName) {
    return sem_context->Error(loc(),
                              strings::Substitute("$0 is a reserved keyspace name",
                                                  common::kRedisKeyspaceName).c_str(),
                              ErrorCode::INVALID_ARGUMENTS);
  }

  if (VLOG_IS_ON(3)) {
    PrintSemanticAnalysisResult(sem_context);
  }

  return Status::OK();
}

void PTUseKeyspace::PrintSemanticAnalysisResult(SemContext *sem_context) {
  MCString sem_output("\tKeyspace ", sem_context->PTempMem());
  sem_output += name();
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << loc() << "):\n" << sem_output;
}

const char* PTUseKeyspace::name() const {
  return name_->c_str();
}

}  // namespace ql
}  // namespace yb
