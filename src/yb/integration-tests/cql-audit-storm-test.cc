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

#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>

#ifdef __linux__
#include <sched.h>
#endif

#include "yb/gutil/casts.h"
#include "yb/gutil/stringprintf.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/split.h"

#include "yb/client/yb_table_name.h"

#include "yb/integration-tests/cql_test_base.h"
#include "yb/integration-tests/external_mini_cluster.h"

#include "yb/util/hdr_histogram.h"
#include "yb/util/random_util.h"
#include "yb/util/stol_utils.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"

using namespace std::literals;

DEFINE_test_flag(int32, cql_storm_clients, 16,
    "Client sessions, each with its own connections, in CqlAuditStormTest.");
DEFINE_test_flag(int32, cql_storm_inflight, 8,
    "Pipelined reads per client during an open. 0 makes the open open-loop: reads are issued at "
    "the paced rate no matter how many are outstanding, up to cql_storm_max_outstanding.");
DEFINE_test_flag(int32, cql_storm_max_outstanding, 2000,
    "Safety cap on outstanding reads per client in open-loop mode.");
DEFINE_test_flag(int32, cql_storm_tablets, 64, "Tablets of the test table.");
DEFINE_test_flag(int32, cql_storm_rows, 20000, "Rows loaded before the test.");
DEFINE_test_flag(int32, cql_storm_value_bytes, 256, "Value size of each row.");
DEFINE_test_flag(int32, cql_storm_warmup_ms, 10000,
    "Unrecorded trickle after each tserver restart, so reconnects, leader elections and tablet "
    "warm-up stay out of the numbers. Must exceed the staggered client connect time.");
DEFINE_test_flag(int32, cql_storm_warmup_open_ms, 10000,
    "Unrecorded full-rate open after the warm-up trickle, so that every recorded open starts from "
    "grown pools rather than the cold-pool storm after a restart. 0 disables it.");
DEFINE_test_flag(int32, cql_storm_pre_open_ms, 5000, "Trickle phase before and between opens.");
DEFINE_test_flag(int32, cql_storm_open_ms, 10000, "Length of each open, the full-rate phase.");
DEFINE_test_flag(int32, cql_storm_opens, 2, "Opens per wake order.");
DEFINE_test_flag(int32, cql_storm_trickle_interval_us, 2000,
    "Delay between reads per client during a trickle phase.");
DEFINE_test_flag(int32, cql_storm_open_interval_us, 0,
    "Delay between reads per client during an open, which paces the open to a fixed rate while "
    "up to cql_storm_inflight reads stay outstanding. 0 runs the open closed loop at full speed.");
DEFINE_test_flag(int32, cql_storm_tservers, 1,
    "Tablet servers, also the replication factor, in CqlAuditStormTest.");
DEFINE_test_flag(string, cql_storm_tserver_cpus, "",
    "CPU lists the tablet servers are pinned to, one per tserver separated by ':', for example "
    "0-3,32-35:4-7,36-39. Empty leaves them unpinned. Linux only.");
DEFINE_test_flag(string, cql_storm_wake_orders, "lifo,mixed",
    "Comma separated thread_pool_worker_wake_order values to run, each on a restarted tserver.");
DEFINE_test_flag(bool, cql_storm_audit, true, "Enable YCQL audit logging on the tserver.");

namespace yb {

namespace {

const auto kKeyspace = "ks"s;
const auto kTable = "ks.t"s;

struct PhaseStats {
  std::string name;
  bool open = false;
  bool recorded = true;
  MonoDelta duration;
  // Successful reads only.
  HdrHistogram latency_us{60'000'000, 3};
  std::atomic<size_t> requests{0};
  std::atomic<size_t> errors{0};
  std::mutex error_mutex;
  std::unordered_map<std::string, size_t> error_messages GUARDED_BY(error_mutex);
  // Peak reads outstanding on one client, open-loop mode only.
  std::atomic<size_t> max_outstanding{0};

  PhaseStats(std::string name_, bool open_, bool recorded_, int32_t duration_ms)
      : name(std::move(name_)), open(open_), recorded(recorded_),
        duration(MonoDelta::FromMilliseconds(duration_ms)) {}

  void AddError(const Status& status) {
    ++errors;
    std::lock_guard lock(error_mutex);
    ++error_messages[status.message().ToBuffer()];
  }

  std::string TopErrors() {
    std::vector<std::pair<size_t, std::string>> sorted;
    {
      std::lock_guard lock(error_mutex);
      for (const auto& [message, count] : error_messages) {
        sorted.emplace_back(count, message);
      }
    }
    std::sort(sorted.rbegin(), sorted.rend());
    sorted.resize(std::min<size_t>(sorted.size(), 3));
    std::vector<std::string> parts;
    for (const auto& [count, message] : sorted) {
      parts.push_back(Format("$0 x '$1'", count, message.substr(0, 120)));
    }
    return JoinStrings(parts, ", ");
  }
};

// Per-thread scheduler counters of the tserver's pool workers: time on CPU and time runnable
// but waiting for a CPU, from /proc/<pid>/task/<tid>/schedstat. Linux only.
struct ThreadSchedStats {
  int64_t on_cpu_ns = 0;
  int64_t run_delay_ns = 0;
};

using WorkerSchedStats = std::unordered_map<int64_t, ThreadSchedStats>;

WorkerSchedStats ReadWorkerSchedStats(pid_t pid, const std::vector<std::string>& prefixes) {
  WorkerSchedStats result;
#ifdef __linux__
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(
           Format("/proc/$0/task", pid), ec)) {
    std::ifstream comm(entry.path() / "comm");
    std::string name;
    std::getline(comm, name);
    bool matches = false;
    for (const auto& prefix : prefixes) {
      matches |= name.find(prefix) == 0;
    }
    if (!matches) {
      continue;
    }
    std::ifstream schedstat(entry.path() / "schedstat");
    ThreadSchedStats stats;
    if (schedstat >> stats.on_cpu_ns >> stats.run_delay_ns) {
      result[std::stoll(entry.path().filename().string())] = stats;
    }
  }
#endif
  return result;
}

// Growth of the counters between two samples. Threads that exited in between are lost, so
// churned pools undercount; threads created in between count from zero.
ThreadSchedStats SchedStatsDelta(const WorkerSchedStats& before, const WorkerSchedStats& after) {
  ThreadSchedStats delta;
  for (const auto& [tid, stats] : after) {
    auto it = before.find(tid);
    delta.on_cpu_ns += stats.on_cpu_ns - (it == before.end() ? 0 : it->second.on_cpu_ns);
    delta.run_delay_ns += stats.run_delay_ns - (it == before.end() ? 0 : it->second.run_delay_ns);
  }
  return delta;
}

// CPU time of a process in clock ticks, user plus system, from /proc/<pid>/stat. Linux only.
int64_t ProcessCpuTicks(pid_t pid) {
#ifdef __linux__
  std::ifstream stat(Format("/proc/$0/stat", pid));
  std::string line;
  std::getline(stat, line);
  // The command name is in parentheses and may contain spaces; fields start after it.
  auto fields_start = line.rfind(')');
  if (fields_start == std::string::npos) {
    return 0;
  }
  std::istringstream fields(line.substr(fields_start + 2));
  std::string field;
  int64_t utime = 0, stime = 0;
  // Fields 3..13 precede utime (14) and stime (15).
  for (int i = 3; i <= 13; ++i) {
    fields >> field;
  }
  fields >> utime >> stime;
  return utime + stime;
#else
  return 0;
#endif
}

double CpuCoresBusy(int64_t ticks, MonoDelta duration) {
#ifdef __linux__
  return static_cast<double>(ticks) / sysconf(_SC_CLK_TCK) / duration.ToSeconds();
#else
  return 0;
#endif
}

#ifdef __linux__
Result<cpu_set_t> ParseCpuList(const std::string& cpus) {
  cpu_set_t set;
  CPU_ZERO(&set);
  std::vector<std::string> ranges = strings::Split(cpus, ",");
  for (const auto& range : ranges) {
    std::vector<std::string> bounds = strings::Split(range, "-");
    int first = VERIFY_RESULT(CheckedStoi(bounds.front()));
    int last = VERIFY_RESULT(CheckedStoi(bounds.back()));
    for (int cpu = first; cpu <= last; ++cpu) {
      CPU_SET(cpu, &set);
    }
  }
  return set;
}

// Affinity is per thread: pins every existing thread; new ones inherit it from their creator.
Status PinProcess(pid_t pid, const cpu_set_t& set) {
  size_t pinned = 0;
  for (const auto& entry : std::filesystem::directory_iterator(Format("/proc/$0/task", pid))) {
    auto tid = VERIFY_RESULT(CheckedStoi(entry.path().filename().string()));
    if (sched_setaffinity(tid, sizeof(set), &set) == 0) {
      ++pinned;
    }
  }
  LOG(INFO) << "Pinned " << pinned << " threads of pid " << pid << " to " << CPU_COUNT(&set)
            << " CPUs";
  return Status::OK();
}
#endif

Status PinProcess(pid_t pid, const std::string& cpus) {
#ifdef __linux__
  return PinProcess(pid, VERIFY_RESULT(ParseCpuList(cpus)));
#else
  return STATUS(NotSupported, "CPU pinning is Linux only");
#endif
}

// Machine-wide CPU time stolen by the hypervisor, in clock ticks, from /proc/stat. Linux only.
int64_t StealTicks() {
#ifdef __linux__
  std::ifstream stat("/proc/stat");
  std::string cpu;
  int64_t user, nice, system, idle, iowait, irq, softirq, steal = 0;
  stat >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
  return steal;
#else
  return 0;
#endif
}

std::string Summary(
    PhaseStats& stats, MonoDelta duration, double tserver_cores, double stolen_cores) {
  const auto& h = stats.latency_us;
  auto result = Format(
      "$0: $1 ok req/s, ok p50/p99/p999/max us: $2/$3/$4/$5, errors $6, tserver cores busy $7, "
      "host steal cores $8",
      stats.name, stats.requests.load() * 1000 / std::max<int64_t>(1, duration.ToMilliseconds()),
      h.ValueAtPercentile(50), h.ValueAtPercentile(99), h.ValueAtPercentile(99.9), h.MaxValue(),
      stats.errors.load(), StringPrintf("%.1f", tserver_cores), StringPrintf("%.2f", stolen_cores));
  if (stats.max_outstanding.load() != 0) {
    result += Format(", peak outstanding/client $0", stats.max_outstanding.load());
  }
  if (stats.errors.load() != 0) {
    result += Format(", top errors: $0", stats.TopErrors());
  }
  return result;
}

} // namespace

// CE-1782 market open shape: audit logging on, a trickle of reads, then full-rate opens. Each
// wake order runs on restarted tservers; result lines compare latency, workers and scheduler wait.
class CqlAuditStormTest : public CqlTestBase<ExternalMiniCluster> {
 protected:
  int num_tablet_servers() override { return FLAGS_TEST_cql_storm_tservers; }

  void SetUpFlags() override {
    orders_ = strings::Split(FLAGS_TEST_cql_storm_wake_orders, ",");
    ASSERT_FALSE(orders_.empty());
    mini_cluster_opt_.extra_master_flags.push_back(
        Format("--replication_factor=$0", FLAGS_TEST_cql_storm_tservers));
    mini_cluster_opt_.extra_master_flags.push_back("--max_create_tablets_per_ts=10000");
    mini_cluster_opt_.extra_master_flags.push_back("--enable_automatic_tablet_splitting=false");
    // The CQL driver takes one port for all hosts.
    mini_cluster_opt_.use_same_ts_ports = true;
    auto& flags = mini_cluster_opt_.extra_tserver_flags;
    flags.push_back(Format("--ycql_enable_audit_log=$0", FLAGS_TEST_cql_storm_audit));
    flags.push_back("--ycql_audit_excluded_categories=QUERY,DML");
    // The mini cluster default (1 GB) makes the CQL server reject hundreds of connections as
    // OVERLOADED.
    flags.push_back("--memory_limit_hard_bytes=8589934592");
  }

  ExternalTabletServer* tserver() { return cluster_->tablet_server(0); }

  Status PinTservers() {
    if (FLAGS_TEST_cql_storm_tserver_cpus.empty()) {
      return Status::OK();
    }
    std::vector<std::string> cpu_lists = strings::Split(FLAGS_TEST_cql_storm_tserver_cpus, ":");
    SCHECK_EQ(cpu_lists.size(), cluster_->num_tablet_servers(), InvalidArgument,
              "One CPU list per tablet server is required");
    for (size_t i = 0; i != cpu_lists.size(); ++i) {
      RETURN_NOT_OK(PinProcess(cluster_->tablet_server(i)->pid(), cpu_lists[i]));
    }
    return PinOthers();
  }

  // The master and this process, including the client threads, go to the CPUs the tservers do not
  // use, so that they never compete with the tservers.
  Status PinOthers() {
#ifdef __linux__
    if (FLAGS_TEST_cql_storm_tserver_cpus.empty()) {
      return Status::OK();
    }
    cpu_set_t others;
    CPU_ZERO(&others);
    for (int cpu = 0; cpu != static_cast<int>(std::thread::hardware_concurrency()); ++cpu) {
      CPU_SET(cpu, &others);
    }
    std::vector<std::string> cpu_lists = strings::Split(FLAGS_TEST_cql_storm_tserver_cpus, ":");
    for (const auto& list : cpu_lists) {
      auto set = VERIFY_RESULT(ParseCpuList(list));
      for (int cpu = 0; cpu != CPU_SETSIZE; ++cpu) {
        if (CPU_ISSET(cpu, &set)) {
          CPU_CLR(cpu, &others);
        }
      }
    }
    RETURN_NOT_OK(PinProcess(cluster_->master()->pid(), others));
    return PinProcess(getpid(), others);
#else
    return Status::OK();
#endif
  }

  std::vector<std::string> Hosts() {
    std::vector<std::string> hosts;
    for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
      hosts.push_back(cluster_->tablet_server(i)->bind_host());
    }
    return hosts;
  }

  Result<CassandraSession> ConnectWithRetries() {
    auto deadline = CoarseMonoClock::now() + 120s;
    for (;;) {
      auto session = driver_->CreateSession();
      if (session.ok() || CoarseMonoClock::now() > deadline) {
        return session;
      }
      std::this_thread::sleep_for(500ms);
    }
  }

  Status LoadData() {
    auto session = VERIFY_RESULT(ConnectWithRetries());
    // Creating thousands of replicas on pinned tservers can outlast both the driver timeout and
    // the server-side wait for table creation, so retry until the table exists.
    const uint32_t kDdlTimeoutMs = 300000;
    RETURN_NOT_OK(session.ExecuteQuery(Format("CREATE KEYSPACE $0", kKeyspace), kDdlTimeoutMs));
    auto create_deadline = CoarseMonoClock::now() + 600s;
    for (;;) {
      auto status = session.ExecuteQuery(Format(
          "CREATE TABLE IF NOT EXISTS $0 (k INT PRIMARY KEY, v TEXT) WITH tablets = $1 AND "
          "transactions = { 'enabled' : false }", kTable, FLAGS_TEST_cql_storm_tablets),
          kDdlTimeoutMs);
      if (status.ok()) {
        break;
      }
      if (CoarseMonoClock::now() > create_deadline) {
        return status;
      }
      LOG(INFO) << "Retrying table creation: " << status;
      std::this_thread::sleep_for(5s);
    }
    for (size_t ts = 0; ts != cluster_->num_tablet_servers(); ++ts) {
      RETURN_NOT_OK(cluster_->WaitForTabletsRunning(cluster_->tablet_server(ts), 600s));
    }
    auto prepared = VERIFY_RESULT(session.Prepare(
        Format("INSERT INTO $0 (k, v) VALUES (?, ?)", kTable)));
    const std::string value(FLAGS_TEST_cql_storm_value_bytes, 'x');
    std::deque<CassandraFuture> futures;
    for (int32_t k = 0; k != FLAGS_TEST_cql_storm_rows; ++k) {
      auto statement = prepared.Bind();
      statement.Bind(0, k);
      statement.Bind(1, value);
      futures.push_back(session.ExecuteGetFuture(statement));
      while (futures.size() > 64) {
        RETURN_NOT_OK(futures.front().Wait());
        futures.pop_front();
      }
    }
    for (auto& future : futures) {
      RETURN_NOT_OK(future.Wait());
    }
    // Flush and compact now, so that no flush or compaction runs during the measured phases.
    const std::vector<client::YBTableName> tables = {
        client::YBTableName(YQL_DATABASE_CQL, kKeyspace, "t")};
    RETURN_NOT_OK(client_->FlushTables(tables, 600s));
    return client_->CompactTables(tables, 600s);
  }

  // One client: pipelined reads during opens, a trickle between them; latency is recorded against
  // the phase the read was issued in.
  void RunClient(int index, const std::atomic<int>& phase, const std::atomic<bool>& stop,
                 std::vector<std::unique_ptr<PhaseStats>>& phases) {
    // Connections arrive over a few seconds rather than all at once.
    std::this_thread::sleep_for(index * 10ms);
    auto session = ASSERT_RESULT(ConnectWithRetries());
    auto prepared = ASSERT_RESULT(session.Prepare(Format("SELECT v FROM $0 WHERE k = ?", kTable)));
    struct Pending {
      MonoTime start;
      PhaseStats* stats;
      CassandraFuture future;
    };
    std::deque<Pending> pending;
    auto complete_oldest = [&pending] {
      auto& oldest = pending.front();
      auto status = oldest.future.Wait();
      if (status.ok()) {
        oldest.stats->latency_us.Increment((MonoTime::Now() - oldest.start).ToMicroseconds());
        ++oldest.stats->requests;
      } else {
        oldest.stats->AddError(status);
      }
      pending.pop_front();
    };
    while (!stop.load(std::memory_order_acquire)) {
      auto* stats = phases[phase.load(std::memory_order_acquire)].get();
      bool open = stats->open;
      auto statement = prepared.Bind();
      statement.Bind(0, RandomUniformInt<int32_t>(0, FLAGS_TEST_cql_storm_rows - 1));
      pending.push_back(Pending{
          .start = MonoTime::Now(), .stats = stats,
          .future = session.ExecuteGetFuture(statement)});
      if (open) {
        // Reap everything finished; in open-loop mode this is the only non-blocking drain.
        while (!pending.empty() && pending.front().future.Ready()) {
          complete_oldest();
        }
        size_t limit = FLAGS_TEST_cql_storm_inflight > 0
            ? FLAGS_TEST_cql_storm_inflight : FLAGS_TEST_cql_storm_max_outstanding;
        while (pending.size() > limit) {
          complete_oldest();
        }
        if (FLAGS_TEST_cql_storm_inflight == 0) {
          auto& peak = stats->max_outstanding;
          size_t seen = peak.load(std::memory_order_relaxed);
          while (seen < pending.size() &&
                 !peak.compare_exchange_weak(seen, pending.size(), std::memory_order_relaxed)) {}
        }
        // Pace by waiting on the oldest read so completions are timed when they happen.
        auto deadline = MonoTime::Now() + MonoDelta::FromMicroseconds(
            FLAGS_TEST_cql_storm_open_interval_us);
        for (;;) {
          auto remaining = deadline - MonoTime::Now();
          if (remaining <= MonoDelta::kZero) {
            break;
          }
          if (pending.empty()) {
            std::this_thread::sleep_for(remaining.ToSteadyDuration());
            break;
          }
          // The timed wait also fails for an errored read, so completion is decided by readiness.
          auto wait_status = pending.front().future.WaitFor(remaining);
          if (!wait_status.ok() && !wait_status.IsTimedOut()) {
            VLOG(1) << "Read failed: " << wait_status;
          }
          if (pending.front().future.Ready()) {
            complete_oldest();
          }
        }
      } else {
        // One read at a time, so its latency does not include the trickle sleep.
        while (!pending.empty()) {
          complete_oldest();
        }
        std::this_thread::sleep_for(FLAGS_TEST_cql_storm_trickle_interval_us * 1us);
      }
    }
    while (!pending.empty()) {
      complete_oldest();
    }
  }

  std::string RunOrder(const std::string& order) {
    std::vector<std::unique_ptr<PhaseStats>> phases;
    phases.push_back(std::make_unique<PhaseStats>(
        "warmup", false, false, FLAGS_TEST_cql_storm_warmup_ms));
    if (FLAGS_TEST_cql_storm_warmup_open_ms) {
      phases.push_back(std::make_unique<PhaseStats>(
          "warmup open", true, false, FLAGS_TEST_cql_storm_warmup_open_ms));
    }
    for (int i = 0; i != FLAGS_TEST_cql_storm_opens; ++i) {
      phases.push_back(std::make_unique<PhaseStats>(
          Format("trickle $0", i + 1), false, true, FLAGS_TEST_cql_storm_pre_open_ms));
      phases.push_back(std::make_unique<PhaseStats>(
          Format("open $0", i + 1), true, true, FLAGS_TEST_cql_storm_open_ms));
    }
    std::atomic<int> phase{0};
    std::atomic<bool> stop{false};
    const std::vector<std::string> worker_prefixes = {"CQLServer_", "TabletServer"};
    auto pid = tserver()->pid();
    auto sched_before = ReadWorkerSchedStats(pid, worker_prefixes);
    size_t max_workers = 0;
    std::vector<std::string> phase_summaries;
    {
      TestThreadHolder holder;
      for (int i = 0; i != FLAGS_TEST_cql_storm_clients; ++i) {
        holder.AddThreadFunctor([this, i, &phase, &stop, &phases] {
          RunClient(i, phase, stop, phases);
        });
      }
      for (size_t i = 0; i != phases.size(); ++i) {
        phase.store(narrow_cast<int>(i), std::memory_order_release);
        auto& stats = *phases[i];
        auto duration = stats.duration;
        auto end = CoarseMonoClock::now() + duration.ToSteadyDuration();
        auto ticks_before = ProcessCpuTicks(pid);
        auto steal_before = StealTicks();
        while (CoarseMonoClock::now() < end) {
          std::this_thread::sleep_for(500ms);
          max_workers = std::max(max_workers, ReadWorkerSchedStats(pid, worker_prefixes).size());
        }
        auto cores = CpuCoresBusy(ProcessCpuTicks(pid) - ticks_before, duration);
        auto stolen = CpuCoresBusy(StealTicks() - steal_before, duration);
        if (!stats.recorded) {
          LOG(INFO) << order << " unrecorded " << Summary(stats, duration, cores, stolen);
          sched_before = ReadWorkerSchedStats(pid, worker_prefixes);
          max_workers = 0;
          continue;
        }
        EXPECT_GT(stats.requests.load(), 0) << order << " " << stats.name;
        phase_summaries.push_back(Summary(stats, duration, cores, stolen));
        LOG(INFO) << order << " " << phase_summaries.back();
      }
      stop = true;
      holder.JoinAll();
    }
    auto sched = SchedStatsDelta(sched_before, ReadWorkerSchedStats(pid, worker_prefixes));
    int64_t requests = 0;
    for (const auto& stats : phases) {
      if (stats->recorded) {
        requests += stats->requests;
      }
    }
    requests = std::max<int64_t>(1, requests);
    return Format(
        "$0: $1; max pool workers $2, worker run-queue wait $3 us/req, on-cpu $4 us/req",
        order, JoinStrings(phase_summaries, "; "), max_workers,
        sched.run_delay_ns / 1000 / requests, sched.on_cpu_ns / 1000 / requests);
  }

  std::vector<std::string> orders_;
};

// Latency is meaningless under sanitizers, and the cluster is too heavy for them.
TEST_F(CqlAuditStormTest, YB_DISABLE_TEST_IN_SANITIZERS(MarketOpen)) {
  ASSERT_OK(PinTservers());
  ASSERT_OK(LoadData());
  std::vector<std::string> results;
  for (const auto& order : orders_) {
    // Every order starts from freshly restarted tservers; the warm-up phase absorbs elections.
    for (size_t ts = 0; ts != cluster_->num_tablet_servers(); ++ts) {
      cluster_->tablet_server(ts)->Shutdown(SafeShutdown::kTrue);
      ASSERT_OK(cluster_->tablet_server(ts)->Restart(
          /* start_cql_proxy = */ true, {{"thread_pool_worker_wake_order", order}}));
    }
    ASSERT_OK(PinTservers());
    for (size_t ts = 0; ts != cluster_->num_tablet_servers(); ++ts) {
      ASSERT_EQ(ASSERT_RESULT(
                    cluster_->tablet_server(ts)->GetFlag("thread_pool_worker_wake_order")),
                order) << "tserver " << ts;
    }
    // Thousands of replicas take a while to bootstrap after a restart.
    for (size_t ts = 0; ts != cluster_->num_tablet_servers(); ++ts) {
      ASSERT_OK(cluster_->WaitForTabletsRunning(cluster_->tablet_server(ts), 300s));
    }
    // Leaders settle wherever elections put them after a restart; an unbalanced tserver tips
    // first under load.
    ASSERT_OK(cluster_->WaitForLoadBalancerToBecomeIdle(client_, 300s));
    driver_ = std::make_unique<CppCassandraDriver>(
        Hosts(), tserver()->cql_rpc_port(), UsePartitionAwareRouting::kFalse);
    // The driver starts its own threads.
    ASSERT_OK(PinOthers());
    results.push_back(RunOrder(order));
  }
  for (size_t ts = 0; ts != cluster_->num_tablet_servers(); ++ts) {
    cluster_->tablet_server(ts)->Shutdown(SafeShutdown::kTrue);
  }
  // The tservers are stopped, so the teardown cluster check cannot reach them.
  DontVerifyClusterBeforeNextTearDown();
  for (const auto& result : results) {
    LOG(INFO) << "RESULT " << result;
  }
}

} // namespace yb
