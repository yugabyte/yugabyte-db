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

#include "yb/yql/pgwrapper/pg_mem_report.h"

#include <unistd.h>

#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/status_format.h"
#include "yb/util/subprocess.h"
#include "yb/util/thread.h"
#include "yb/util/tsan_util.h"

DEFINE_test_flag(bool, ysql_pg_mem_report, false,
    "Record the idle and peak memory of every postgres process, compare them with the "
    "--TEST_ysql_pg_mem_*_kb budgets, and log a YB_PG_MEM_RECORD line for each process when the "
    "postmaster reaps it. Read once at postmaster start.");

DEFINE_test_flag(bool, ysql_pg_mem_enforce,
    yb::ReleaseVsDebugVsAsanVsTsan(true, false, false, false),
    "Whether a postgres process over a --TEST_ysql_pg_mem_*_kb budget fails the test. When false "
    "the violation is only logged.");

// Release budgets are 1.1x the largest value measured for empty processes (backends that connect
// and run SELECT 1) in MiniCluster and ExternalMiniCluster. Other build types are not measured yet.
DEFINE_test_flag(int64, ysql_pg_mem_backend_idle_kb,
    yb::ReleaseVsDebugVsAsanVsTsan<int64_t>(21661, 0, 0, 0),
    "Budget for the anonymous RSS of a client backend or walsender while idle between "
    "statements, in kB. 0 means no budget.");

DEFINE_test_flag(int64, ysql_pg_mem_backend_peak_kb,
    yb::ReleaseVsDebugVsAsanVsTsan<int64_t>(66722, 0, 0, 0),
    "Budget for the peak RSS (VmHWM) of a client backend or walsender, in kB. 0 means no budget.");

DEFINE_test_flag(int64, ysql_pg_mem_relcache_init_idle_kb,
    yb::ReleaseVsDebugVsAsanVsTsan<int64_t>(41048, 0, 0, 0),
    "Budget for the anonymous RSS of a relcache init backend (the backend that builds the "
    "relcache init file) while idle, in kB. 0 means no budget.");

DEFINE_test_flag(int64, ysql_pg_mem_relcache_init_peak_kb,
    yb::ReleaseVsDebugVsAsanVsTsan<int64_t>(88651, 0, 0, 0),
    "Budget for the peak RSS (VmHWM) of a relcache init backend, in kB. 0 means no budget.");

DEFINE_test_flag(int64, ysql_pg_mem_parallel_worker_peak_kb, 0,
    "Budget for the peak RSS (VmHWM) of a parallel worker, in kB. 0 means no budget.");

DEFINE_test_flag(int64, ysql_pg_mem_bgworker_peak_kb,
    yb::ReleaseVsDebugVsAsanVsTsan<int64_t>(62256, 0, 0, 0),
    "Budget for the peak RSS (VmHWM) of a background worker other than a parallel worker, in kB. "
    "0 means no budget.");

DEFINE_test_flag(int64, ysql_pg_mem_aux_peak_kb,
    yb::ReleaseVsDebugVsAsanVsTsan<int64_t>(29000, 0, 0, 0),
    "Budget for the peak RSS (VmHWM) of an auxiliary process (checkpointer, background writer, "
    "WAL writer, startup), in kB. 0 means no budget.");

DEFINE_test_flag(int64, ysql_pg_mem_postmaster_peak_kb,
    yb::ReleaseVsDebugVsAsanVsTsan<int64_t>(140466, 0, 0, 0),
    "Budget for the peak RSS (VmHWM) of the postmaster, in kB. 0 means no budget.");

namespace yb::pgwrapper {

namespace {

const std::vector<std::string> kPgMemFlags = {
    "TEST_ysql_pg_mem_report",
    "TEST_ysql_pg_mem_enforce",
    "TEST_ysql_pg_mem_backend_idle_kb",
    "TEST_ysql_pg_mem_backend_peak_kb",
    "TEST_ysql_pg_mem_relcache_init_idle_kb",
    "TEST_ysql_pg_mem_relcache_init_peak_kb",
    "TEST_ysql_pg_mem_parallel_worker_peak_kb",
    "TEST_ysql_pg_mem_bgworker_peak_kb",
    "TEST_ysql_pg_mem_aux_peak_kb",
    "TEST_ysql_pg_mem_postmaster_peak_kb",
};

constexpr std::string_view kStartMarker = "YB_PG_MEM_START ";
constexpr std::string_view kRecordMarker = "YB_PG_MEM_RECORD ";
constexpr std::string_view kSnapshotMarker = "YB_PG_MEM_SNAPSHOT ";

std::string FlagValue(const std::string& name) {
  std::string value;
  CHECK(google::GetCommandLineOption(name.c_str(), &value)) << name;
  return value;
}

std::string Field(const std::map<std::string, std::string>& fields, const std::string& key) {
  auto it = fields.find(key);
  return it == fields.end() ? "?" : it->second;
}

std::string FormatViolation(const std::map<std::string, std::string>& r) {
  const auto over = Field(r, "over");
  std::string type = Field(r, "type");
  if (auto it = r.find("bgw"); it != r.end() && !it->second.empty()) {
    type += Format(" ($0)", it->second);
  }
  std::string result = Format(
      "  pid $0 $1 db=$2 app=\"$3\"\n", Field(r, "pid"), type, Field(r, "db"), Field(r, "app"));
  if (over.find("peak") != std::string::npos) {
    result += Format(
        "      peak $0 kB > budget $1 kB (shared $2 kB at peak)\n",
        Field(r, "peak_kb"), Field(r, "peak_budget_kb"), Field(r, "peak_shared_kb"));
    if (auto it = r.find("stmt"); it != r.end() && !it->second.empty()) {
      result += Format("      peak grew after: $0\n", it->second);
    }
  }
  if (over.find("idle") != std::string::npos) {
    result += Format(
        "      idle anon $0 kB > budget $1 kB (first idle $2 kB)\n",
        Field(r, "idle_anon_kb"), Field(r, "idle_budget_kb"), Field(r, "first_idle_anon_kb"));
  }
  return result;
}

void WriteFully(int fd, const char* data, size_t size) {
  while (size > 0) {
    auto written = write(fd, data, size);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      return;
    }
    data += written;
    size -= written;
  }
}

}  // namespace

const std::vector<std::string>& PgMemReportFlagNames() {
  return kPgMemFlags;
}

std::vector<std::string> PgMemReportFlagsForChildProcess() {
  std::vector<std::string> result;
  for (const auto& name : kPgMemFlags) {
    result.push_back(Format("--$0=$1", name, FlagValue(name)));
  }
  return result;
}

void SetPgMemReportEnv(Subprocess* proc) {
  for (const auto& name : kPgMemFlags) {
    proc->SetEnv("FLAGS_" + name, FlagValue(name));
  }
}

PgMemLogSource::~PgMemLogSource() {
  Close();
}

void PgMemLogSource::AddLine(std::string_view line) {
  if (line.find("YB_PG_MEM_") == std::string_view::npos) {
    return;
  }
  PgMemReportCollector::Get().AddLine(generation_, line);
}

void PgMemLogSource::Close() {
  if (!closed_.exchange(true)) {
    PgMemReportCollector::Get().SourceClosed(generation_);
  }
}

PgMemReportCollector& PgMemReportCollector::Get() {
  static auto* collector = new PgMemReportCollector();
  return *collector;
}

void PgMemReportCollector::Start() {
  std::lock_guard lock(mutex_);
  started_ = true;
  ++generation_;
  open_sources_ = 0;
  lines_.clear();
}

void PgMemReportCollector::Stop() {
  std::lock_guard lock(mutex_);
  started_ = false;
  ++generation_;
  open_sources_ = 0;
  cond_.notify_all();
}

bool PgMemReportCollector::started() const {
  std::lock_guard lock(mutex_);
  return started_;
}

std::shared_ptr<PgMemLogSource> PgMemReportCollector::NewSource() {
  std::lock_guard lock(mutex_);
  if (!started_) {
    return nullptr;
  }
  ++open_sources_;
  return std::shared_ptr<PgMemLogSource>(new PgMemLogSource(generation_));
}

Status PgMemReportCollector::WaitForSourcesClosed(MonoDelta timeout) {
  std::unique_lock lock(mutex_);
  if (!cond_.wait_for(
          lock, timeout.ToSteadyDuration(), [this] { return open_sources_ == 0; })) {
    return STATUS_FORMAT(
        TimedOut, "$0 postgres output streams still open after $1", open_sources_, timeout);
  }
  return Status::OK();
}

void PgMemReportCollector::AddLine(uint64_t generation, std::string_view line) {
  while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
    line.remove_suffix(1);
  }
  std::lock_guard lock(mutex_);
  if (started_ && generation == generation_) {
    lines_.emplace_back(line);
  }
}

void PgMemReportCollector::SourceClosed(uint64_t generation) {
  std::lock_guard lock(mutex_);
  if (generation == generation_ && open_sources_ > 0) {
    --open_sources_;
    cond_.notify_all();
  }
}

std::vector<std::string> PgMemReportCollector::Lines() const {
  std::lock_guard lock(mutex_);
  return lines_;
}

PgMemReport PgMemReportCollector::Report() const {
  PgMemReport report;
  // Postmaster pid -> number of records seen for it.
  std::map<std::string, size_t> postmasters;
  for (const auto& line : Lines()) {
    if (line.find(kSnapshotMarker) != std::string::npos) {
      continue;
    }
    auto fields = ParsePgMemLine(line);
    if (!fields.ok()) {
      report.warnings.push_back(Format("Unparsable line: $0 ($1)", line, fields.status()));
      continue;
    }
    const auto pm = Field(*fields, "pm");
    if (line.find(kStartMarker) != std::string::npos) {
      postmasters.emplace(pm, 0);
      report.enforce = report.enforce || Field(*fields, "enforce") == "1";
      continue;
    }
    ++report.num_records;
    ++postmasters[pm];
    const auto over = Field(*fields, "over");
    if (over != "-") {
      report.violations.push_back(FormatViolation(*fields));
    }
  }
  for (const auto& [pm, num_records] : postmasters) {
    if (num_records == 0) {
      report.warnings.push_back(Format(
          "Postmaster $0 logged YB_PG_MEM_START but no YB_PG_MEM_RECORD lines: records were "
          "suppressed (log_min_messages) or lost", pm));
    }
  }
  return report;
}

std::string PgMemReport::ToString() const {
  std::string result;
  if (!violations.empty()) {
    result += Format(
        "PG memory budget exceeded (enforce: $0): $1 of $2 processes over budget\n",
        enforce ? "on" : "off", violations.size(), num_records);
    for (const auto& violation : violations) {
      result += violation;
    }
    result +=
        "  heap snapshots: search the tserver output for 'YB_PG_MEM_SNAPSHOT pid=<pid>'\n"
        "  to change a budget: set the --TEST_ysql_pg_mem_*_kb tserver flags\n";
  }
  for (const auto& warning : warnings) {
    result += Format("PG memory report warning: $0\n", warning);
  }
  return result;
}

Result<std::map<std::string, std::string>> ParsePgMemLine(std::string_view line) {
  bool found = false;
  for (auto marker : {kRecordMarker, kStartMarker, kSnapshotMarker}) {
    auto pos = line.find(marker);
    if (pos != std::string_view::npos) {
      line.remove_prefix(pos + marker.size());
      found = true;
      break;
    }
  }
  if (!found) {
    return STATUS(InvalidArgument, "No YB_PG_MEM marker");
  }

  std::map<std::string, std::string> result;
  size_t i = 0;
  while (i < line.size()) {
    if (line[i] == ' ' || line[i] == '\n' || line[i] == '\r') {
      ++i;
      continue;
    }
    auto eq = line.find('=', i);
    if (eq == std::string_view::npos) {
      return STATUS_FORMAT(InvalidArgument, "Expected key=value at offset $0", i);
    }
    std::string key(line.substr(i, eq - i));
    i = eq + 1;
    std::string value;
    if (i < line.size() && line[i] == '"') {
      ++i;
      bool closed = false;
      while (i < line.size()) {
        char c = line[i++];
        if (c == '"') {
          closed = true;
          break;
        }
        if (c == '\\' && i < line.size()) {
          char escaped = line[i++];
          value += escaped == 'n' ? '\n' : escaped;
        } else {
          value += c;
        }
      }
      if (!closed) {
        return STATUS_FORMAT(InvalidArgument, "Unterminated quoted value for $0", key);
      }
    } else {
      auto end = line.find(' ', i);
      if (end == std::string_view::npos) {
        end = line.size();
      }
      value = std::string(line.substr(i, end - i));
      i = end;
    }
    result[std::move(key)] = std::move(value);
  }
  return result;
}

void StartPgMemStderrTee(int fd, std::shared_ptr<PgMemLogSource> source) {
  // Plain read() rather than stdio: a FILE* blocked in fgets() holds its lock, and fflush(nullptr)
  // (e.g. from gtest's CaptureStderr()) waits for every FILE lock, so it would hang until postgres
  // writes another line.
  auto tee = [fd, source] {
    std::string pending;
    char buffer[65536];
    for (;;) {
      auto n = read(fd, buffer, sizeof(buffer));
      if (n < 0 && errno == EINTR) {
        continue;
      }
      if (n <= 0) {
        break;
      }
      WriteFully(STDERR_FILENO, buffer, n);
      pending.append(buffer, n);
      size_t start = 0;
      for (auto newline = pending.find('\n'); newline != std::string::npos;
           newline = pending.find('\n', start)) {
        source->AddLine(std::string_view(pending).substr(start, newline + 1 - start));
        start = newline + 1;
      }
      pending.erase(0, start);
    }
    if (!pending.empty()) {
      source->AddLine(pending);
    }
    close(fd);
    source->Close();
  };
  // Dropping the returned ThreadPtr detaches the thread.
  auto thread = Thread::Make("pgwrapper", "pg_stderr_tee", tee);
  if (!thread.ok()) {
    LOG(WARNING) << "Failed to start postgres stderr tee: " << thread.status();
    close(fd);
    source->Close();
  }
}

}  // namespace yb::pgwrapper
