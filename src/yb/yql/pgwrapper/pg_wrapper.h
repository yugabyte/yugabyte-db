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

#ifndef YB_YQL_PGWRAPPER_PG_WRAPPER_H
#define YB_YQL_PGWRAPPER_PG_WRAPPER_H

#include <string>

#include <boost/optional.hpp>

#include "yb/util/subprocess.h"
#include "yb/util/status.h"

namespace yb {
namespace pgwrapper {

// Returns the root directory of the PostgreSQL part of the current YugaByte installation.
std::string GetPostgresInstallRoot();

// Configuration for an external PostgreSQL server.
struct PgWrapperConf {
  std::string data_dir;
  uint16_t pg_port;
};

class PgWrapper {
 public:
  explicit PgWrapper(PgWrapperConf conf)
      : conf_(conf) {
  }

  void EnsureDbDirExists();

  CHECKED_STATUS Start();

  // Calls PostgreSQL's initdb program for initial database initialization.
  CHECKED_STATUS InitDB();

 private:
  // Set common environment for a child process (initdb or postgres itself).
  void SetCommonEnv(Subprocess* proc);

  PgWrapperConf conf_;
  boost::optional<Subprocess> pg_proc_;
};

}  // namespace pgwrapper
}  // namespace yb

#endif  // YB_YQL_PGWRAPPER_PG_WRAPPER_H
