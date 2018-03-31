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
// Entry to SQL module. It takes SQL statements and uses the given YBClient to execute them. Each
// QLProcessor runs on one and only one thread, so all function in SQL modules don't need to be
// thread-safe.
//--------------------------------------------------------------------------------------------------
#ifndef YB_YQL_PGSQL_PROCESSOR_PG_PROCESSOR_H_
#define YB_YQL_PGSQL_PROCESSOR_PG_PROCESSOR_H_

#include "yb/client/client.h"

#include "yb/yql/pgsql/ybpostgres/pg_port.h"
#include "yb/yql/pgsql/ybpostgres/pg_recv.h"
#include "yb/yql/pgsql/ybpostgres/pg_send.h"

#include "yb/yql/pgsql/util/pg_env.h"
#include "yb/yql/pgsql/util/pg_session.h"

#include "yb/yql/pgsql/ptree/parse_tree.h"
#include "yb/yql/pgsql/syn/pg_parser.h"
#include "yb/yql/pgsql/sem/pg_analyzer.h"
#include "yb/yql/pgsql/pbgen/pg_coder.h"
#include "yb/yql/pgsql/pbexec/pg_executor.h"

namespace yb {
namespace pgsql {

class PgProcessor {
 public:
  // Public types.
  typedef std::shared_ptr<PgProcessor> SharedPtr;
  typedef std::shared_ptr<const PgProcessor> SharedPtrConst;

  // Constructors.
  explicit PgProcessor(const PgEnv::SharedPtr& pg_env);
  virtual ~PgProcessor();

  // Run the given instruction.
  CHECKED_STATUS Process(const PgSession::SharedPtr& client_session,
                         const pgapi::PGInstr::SharedPtr& instr,
                         string *exec_status,
                         string *exec_output);

  CHECKED_STATUS Run(const PgSession::SharedPtr& client_session,
                     pgapi::PGInstrQuery *instr,
                     string *exec_status,
                     string *exec_output);

  CHECKED_STATUS RunTest(const PgSession::SharedPtr& client_session, const string& stmt);

 protected:
  PgEnv::SharedPtr pg_env_;
  PgParser parser_;
  PgAnalyzer analyzer_;
  PgCoder coder_;
  PgExecutor executor_;
};

}  // namespace pgsql
}  // namespace yb

#endif  // YB_YQL_PGSQL_PROCESSOR_PG_PROCESSOR_H_
