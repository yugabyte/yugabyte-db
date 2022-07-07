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
//--------------------------------------------------------------------------------------------------
#include "yb/yql/cql/ql/sem/analyzer.h"

#include "yb/util/status.h"
#include "yb/yql/cql/ql/ptree/parse_tree.h"
#include "yb/yql/cql/ql/ptree/sem_context.h"
#include "yb/yql/cql/ql/util/errcodes.h"

namespace yb {
namespace ql {

using std::string;

//--------------------------------------------------------------------------------------------------

Analyzer::Analyzer(QLEnv *ql_env) : ql_env_(ql_env) {
}

Analyzer::~Analyzer() {
}

//--------------------------------------------------------------------------------------------------

Status Analyzer::Analyze(ParseTreePtr parse_tree) {
  ParseTree *ptree = parse_tree.get();
  DCHECK(ptree != nullptr) << "Parse tree is null";
  sem_context_ = std::make_unique<SemContext>(std::move(parse_tree), ql_env_);
  Status s = ptree->Analyze(sem_context_.get());
  if (PREDICT_FALSE(!s.ok())) {
    // When a statement is parsed for the first time, semantic analysis may fail because stale
    // table metadata cache was used. If that happens, clear the cache and tell the caller to
    // reparse. The only exception is when the keyspace, table or type or is not found in which
    // case no cache is used.
    if (!ptree->reparsed()) {
      const ErrorCode errcode = GetErrorCode(s);
      if (errcode != ErrorCode::KEYSPACE_NOT_FOUND &&
          errcode != ErrorCode::OBJECT_NOT_FOUND &&
          errcode != ErrorCode::TYPE_NOT_FOUND &&
          sem_context_->cache_used()) {
        ptree->ClearAnalyzedTableCache(ql_env_);
        ptree->ClearAnalyzedUDTypeCache(ql_env_);
        ptree->set_stale();
        return sem_context_->Error(ptree->root(), ErrorCode::STALE_METADATA);
      }
    }

    // Before leaving the semantic step, collect all errors and place them in return status.
    VLOG(3) << "Failed to analyze parse-tree <" << ptree << ">";
    return sem_context_->GetStatus();
  }

  VLOG(3) << "Successfully analyzed parse-tree <" << ptree << ">";
  return Status::OK();
}

ParseTree::UniPtr Analyzer::Done() {
  // When releasing the parse tree, we must free the context because it has references to the tree
  // which doesn't belong to this context any longer.
  ParseTree::UniPtr ptree = sem_context_->AcquireParseTree();
  sem_context_ = nullptr;
  return ptree;
}

bool Analyzer::cache_used() const {
  return sem_context_->cache_used();
}

}  // namespace ql
}  // namespace yb
