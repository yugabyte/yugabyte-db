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

// Test-only memory budgets for postgres processes.
//
// With --TEST_ysql_pg_mem_report, every postgres process records its idle memory and peak RSS in
// shared memory, and the postmaster compares them with the --TEST_ysql_pg_mem_*_kb budgets when it
// reaps the process. It logs one line per process:
//
//   YB_PG_MEM_RECORD pm=<pid> pid=<pid> type=<type> ... over=<idle|peak|idle,peak|-> ...
//
// plus one "YB_PG_MEM_START pm=<pid> enforce=<0|1>" line at postmaster startup, and a
// "YB_PG_MEM_SNAPSHOT pid=<pid> ..." line before each heap snapshot a process over budget logs.
// Test harnesses collect these lines from the postgres stderr stream (PgMemReportCollector below)
// and fail the test when a record has "over" set and enforcement is on.

#pragma once

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "yb/util/monotime.h"
#include "yb/util/result.h"

namespace yb {

class Subprocess;

namespace pgwrapper {

const std::vector<std::string>& PgMemReportFlagNames();

// The --TEST_ysql_pg_mem_* flags of this process, as command-line arguments for a child tserver.
std::vector<std::string> PgMemReportFlagsForChildProcess();

// Exports the --TEST_ysql_pg_mem_* flags to the postmaster, which reads them with getenv().
void SetPgMemReportEnv(Subprocess* proc);

// One stream of postgres output, e.g. the stderr of one postmaster.
class PgMemLogSource {
 public:
  ~PgMemLogSource();

  void AddLine(std::string_view line);

  // Marks the end of the stream. Called on EOF; idempotent.
  void Close();

 private:
  friend class PgMemReportCollector;

  explicit PgMemLogSource(uint64_t generation) : generation_(generation) {}

  const uint64_t generation_;
  std::atomic<bool> closed_{false};
};

struct PgMemReport {
  bool enforce = false;
  size_t num_records = 0;
  // One entry per process over budget.
  std::vector<std::string> violations;
  std::vector<std::string> warnings;

  bool empty() const { return violations.empty() && warnings.empty(); }
  std::string ToString() const;
};

// Collects YB_PG_MEM_* lines from the postgres output of the clusters a test starts. A process-wide
// singleton: postgres output is tailed by detached threads that can outlive the test fixture.
class PgMemReportCollector {
 public:
  static PgMemReportCollector& Get();

  // Clears collected lines and starts accepting new sources.
  void Start();
  void Stop();
  bool started() const;

  // Returns nullptr unless started.
  std::shared_ptr<PgMemLogSource> NewSource();

  // Waits until every source created since Start() has reached EOF.
  Status WaitForSourcesClosed(MonoDelta timeout);

  PgMemReport Report() const;
  std::vector<std::string> Lines() const;

 private:
  friend class PgMemLogSource;

  PgMemReportCollector() = default;

  void AddLine(uint64_t generation, std::string_view line);
  void SourceClosed(uint64_t generation);

  mutable std::mutex mutex_;
  std::condition_variable cond_;
  bool started_ = false;
  uint64_t generation_ = 0;
  size_t open_sources_ = 0;
  std::vector<std::string> lines_;
};

// Parses the key=value pairs of a YB_PG_MEM_START, _RECORD or _SNAPSHOT line; anything before the
// marker (log prefixes) is ignored. Quoted values may contain \" \\ and \n escapes. The statement
// is logged hex-encoded as stmt_hex, so that the log never contains raw SQL; it is also returned
// decoded as stmt.
Result<std::map<std::string, std::string>> ParsePgMemLine(std::string_view line);

// Reads fd until EOF on a detached thread, copies each line to this process's stderr and hands it
// to source. Takes ownership of fd.
void StartPgMemStderrTee(int fd, std::shared_ptr<PgMemLogSource> source);

}  // namespace pgwrapper
}  // namespace yb
