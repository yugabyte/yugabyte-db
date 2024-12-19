// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/util/thread.h"

#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>

#include "yb/util/signal_util.h"

#if defined(__linux__)
#include <sys/prctl.h>
#endif // defined(__linux__)

#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include <cds/init.h>
#include <cds/gc/dhp.h>

#include "yb/gutil/atomicops.h"
#include "yb/gutil/bind.h"
#include "yb/gutil/dynamic_annotations.h"
#include "yb/gutil/once.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/util/debug-util.h"
#include "yb/util/errno.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/mutex.h"
#include "yb/util/os-util.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/stopwatch.h"
#include "yb/util/url-coding.h"
#include "yb/util/web_callback_registry.h"

METRIC_DEFINE_gauge_uint64(server, threads_started,
                           "Threads Started",
                           yb::MetricUnit::kThreads,
                           "Total number of threads started on this server",
                           yb::EXPOSE_AS_COUNTER);

METRIC_DEFINE_gauge_uint64(server, threads_running,
                           "Threads Running",
                           yb::MetricUnit::kThreads,
                           "Current number of running threads");

METRIC_DEFINE_gauge_uint64(server, cpu_utime,
                           "User CPU Time",
                           yb::MetricUnit::kMilliseconds,
                           "Total user CPU time of the process",
                           yb::EXPOSE_AS_COUNTER);

METRIC_DEFINE_gauge_uint64(server, cpu_stime,
                           "System CPU Time",
                           yb::MetricUnit::kMilliseconds,
                           "Total system CPU time of the process",
                           yb::EXPOSE_AS_COUNTER);

METRIC_DEFINE_gauge_uint64(server, voluntary_context_switches,
                           "Voluntary Context Switches",
                           yb::MetricUnit::kContextSwitches,
                           "Total voluntary context switches",
                           yb::EXPOSE_AS_COUNTER);

METRIC_DEFINE_gauge_uint64(server, involuntary_context_switches,
                           "Involuntary Context Switches",
                           yb::MetricUnit::kContextSwitches,
                           "Total involuntary context switches",
                           yb::EXPOSE_AS_COUNTER);

DEFINE_NON_RUNTIME_int32(min_thread_stack_size_bytes, 512 * 1024,
    "Default minimum stack size for new threads. If set to <=0, the system default will be used. "
    "Note that the stack can grow larger than this if needed and allowed by system limits.");

namespace yb {

using std::endl;
using std::map;
using std::vector;
using std::shared_ptr;
using std::stringstream;
using std::string;
using std::vector;
using strings::Substitute;

using namespace std::placeholders;

// See comment below in SetThreadName.
constexpr int kMaxThreadNameInPerf = 15;
const char Thread::kPaddingChar = 'x';

namespace {

const std::string kAllGroups = "all";

uint64_t GetCpuUTime() {
  rusage ru;
  CHECK_ERR(getrusage(RUSAGE_SELF, &ru));
  return ru.ru_utime.tv_sec * 1000UL + ru.ru_utime.tv_usec / 1000UL;
}

uint64_t GetCpuSTime() {
  rusage ru;
  CHECK_ERR(getrusage(RUSAGE_SELF, &ru));
  return ru.ru_stime.tv_sec * 1000UL + ru.ru_stime.tv_usec / 1000UL;
}

uint64_t GetVoluntaryContextSwitches() {
  rusage ru;
  CHECK_ERR(getrusage(RUSAGE_SELF, &ru));
  return ru.ru_nvcsw;;
}

uint64_t GetInVoluntaryContextSwitches() {
  rusage ru;
  CHECK_ERR(getrusage(RUSAGE_SELF, &ru));
  return ru.ru_nivcsw;
}

class ThreadCategoryTracker {
 public:
  explicit ThreadCategoryTracker(const string& name) : name_(std::move(name)) {}

  void IncrementCategory(const string& category);
  void DecrementCategory(const string& category);
  void RegisterMetricEntity(const scoped_refptr<MetricEntity> &metric_entity);
  void RegisterGaugeForAllMetricEntities(const string& category);

 private:
  uint64 GetCategory(const string& category);
  string name_;
  Mutex lock_;
  map<string, std::unique_ptr<GaugePrototype<uint64>>> gauge_protos_;
  map<string, uint64_t> metrics_;
  vector<scoped_refptr<MetricEntity>> metric_entities_;
};

uint64 ThreadCategoryTracker::GetCategory(const string& category) {
  MutexLock l(lock_);
  return metrics_[category];
}

void ThreadCategoryTracker::RegisterMetricEntity(const scoped_refptr<MetricEntity> &metric_entity) {
  MutexLock l(lock_);
  for (const auto& [category, gauge_proto] : gauge_protos_) {
    gauge_proto->InstantiateFunctionGauge(metric_entity,
      Bind(&ThreadCategoryTracker::GetCategory, Unretained(this), category));
  }
  metric_entities_.push_back(metric_entity);
}

void ThreadCategoryTracker::IncrementCategory(const string& category) {
  MutexLock l(lock_);
  RegisterGaugeForAllMetricEntities(category);
  metrics_[category]++;
}

void ThreadCategoryTracker::DecrementCategory(const string& category) {
  MutexLock l(lock_);
  RegisterGaugeForAllMetricEntities(category);
  metrics_[category]--;
}

void ThreadCategoryTracker::RegisterGaugeForAllMetricEntities(const string& category) {
  if (gauge_protos_.find(category) == gauge_protos_.end()) {
    string id = name_ + "_" + category;
    EscapeMetricNameForPrometheus(&id);
    const string description = id + " metric in ThreadCategoryTracker";
    std::unique_ptr<GaugePrototype<uint64>> gauge_proto =
      std::make_unique<OwningGaugePrototype<uint64>>( "server", id, description,
      yb::MetricUnit::kThreads, description, yb::MetricLevel::kInfo, yb::EXPOSE_AS_COUNTER);

    for (auto& metric_entity : metric_entities_) {
      gauge_proto->InstantiateFunctionGauge(metric_entity,
        Bind(&ThreadCategoryTracker::GetCategory, Unretained(this), category));
    }
    gauge_protos_[category] = std::move(gauge_proto);
    metrics_[category] = static_cast<uint64>(0);
  }
}

// A singleton class that tracks all live threads, and groups them together for easy
// auditing. Used only by Thread.
class ThreadMgr {
 public:
  ThreadMgr()
      : metrics_enabled_(false),
        threads_started_metric_(0),
        threads_running_metric_(0) {
    cds::Initialize();
    cds::gc::dhp::GarbageCollector::construct();
    cds::threading::Manager::attachThread();
    started_category_tracker_ = std::make_unique<ThreadCategoryTracker>("threads_started");
    running_category_tracker_ = std::make_unique<ThreadCategoryTracker>("threads_running");
  }

  ~ThreadMgr() {
    cds::Terminate();
    MutexLock l(lock_);
    thread_categories_.clear();
  }

  static void SetThreadName(const std::string& name, int64 tid);

  Status StartInstrumentation(const scoped_refptr<MetricEntity>& metrics, WebCallbackRegistry* web);

  // Registers a thread to the supplied category. The key is a pthread_t,
  // not the system TID, since pthread_t is less prone to being recycled.
  void AddThread(const pthread_t& pthread_id, const string& name, const string& category,
      int64_t tid);

  // Removes a thread from the supplied category. If the thread has
  // already been removed, this is a no-op.
  void RemoveThread(const pthread_t& pthread_id, const string& category);

  void RenderThreadGroup(const std::string& group, std::ostream& output);
  uint64_t ReadThreadsRunning();

 private:
  // Container class for any details we want to capture about a thread
  // TODO: Add start-time.
  // TODO: Track fragment ID.
  class ThreadDescriptor {
   public:
    ThreadDescriptor() { }
    ThreadDescriptor(string category, string name, int64_t thread_id)
        : name_(std::move(name)),
          category_(std::move(category)),
          thread_id_(thread_id) {}

    const string& name() const { return name_; }
    const string& category() const { return category_; }
    int64_t thread_id() const { return thread_id_; }

   private:
    string name_;
    string category_;
    int64_t thread_id_;
  };

  // A ThreadCategory is a set of threads that are logically related.
  // TODO: unordered_map is incompatible with pthread_t, but would be more
  // efficient here.
  typedef map<const pthread_t, ThreadDescriptor> ThreadCategory;

  // All thread categorys, keyed on the category name.
  typedef map<string, ThreadCategory> ThreadCategoryMap;

  // Protects thread_categories_ and metrics_enabled_
  Mutex lock_;

  // All thread categorys that ever contained a thread, even if empty
  ThreadCategoryMap thread_categories_;

  // True after StartInstrumentation(..) returns
  bool metrics_enabled_;

  // Counters to track all-time total number of threads, and the
  // current number of running threads.
  uint64_t threads_started_metric_;
  uint64_t threads_running_metric_;

  // Tracker to track the number of started threads and the number of running threads for each
  // category.
  std::unique_ptr<ThreadCategoryTracker> started_category_tracker_;
  std::unique_ptr<ThreadCategoryTracker> running_category_tracker_;

  // Metric callbacks.
  uint64_t ReadThreadsStarted();

  // Webpage callback; prints all threads by category
  void ThreadPathHandler(const WebCallbackRegistry::WebRequest& args,
                                WebCallbackRegistry::WebResponse* resp);
  void RenderThreadCategoryRows(const ThreadCategory& category, std::string* output);
};

void ThreadMgr::SetThreadName(const string& name, int64 tid) {
  // On linux we can get the thread names to show up in the debugger by setting
  // the process name for the LWP.  We don't want to do this for the main
  // thread because that would rename the process, causing tools like killall
  // to stop working.
  if (tid == getpid()) {
    return;
  }

  yb::SetThreadName(name);
}

Status ThreadMgr::StartInstrumentation(const scoped_refptr<MetricEntity>& metrics,
                                       WebCallbackRegistry* web) {
  MutexLock l(lock_);
  metrics_enabled_ = true;
  started_category_tracker_->RegisterMetricEntity(metrics);
  running_category_tracker_->RegisterMetricEntity(metrics);

  // Use function gauges here so that we can register a unique copy of these metrics in
  // multiple tservers, even though the ThreadMgr is itself a singleton.
  metrics->NeverRetire(
      METRIC_threads_started.InstantiateFunctionGauge(metrics,
        Bind(&ThreadMgr::ReadThreadsStarted, Unretained(this))));
  metrics->NeverRetire(
      METRIC_threads_running.InstantiateFunctionGauge(metrics,
        Bind(&ThreadMgr::ReadThreadsRunning, Unretained(this))));
  metrics->NeverRetire(
      METRIC_cpu_utime.InstantiateFunctionGauge(metrics,
        Bind(&GetCpuUTime)));
  metrics->NeverRetire(
      METRIC_cpu_stime.InstantiateFunctionGauge(metrics,
        Bind(&GetCpuSTime)));
  metrics->NeverRetire(
      METRIC_voluntary_context_switches.InstantiateFunctionGauge(metrics,
        Bind(&GetVoluntaryContextSwitches)));
  metrics->NeverRetire(
      METRIC_involuntary_context_switches.InstantiateFunctionGauge(metrics,
        Bind(&GetInVoluntaryContextSwitches)));

  WebCallbackRegistry::PathHandlerCallback thread_callback =
      std::bind(&ThreadMgr::ThreadPathHandler, this, _1, _2);
  DCHECK_NOTNULL(web)->RegisterPathHandler("/threadz", "Threads", thread_callback, true, false);
  return Status::OK();
}

uint64_t ThreadMgr::ReadThreadsStarted() {
  MutexLock l(lock_);
  return threads_started_metric_;
}

uint64_t ThreadMgr::ReadThreadsRunning() {
  MutexLock l(lock_);
  return threads_running_metric_;
}

void ThreadMgr::AddThread(const pthread_t& pthread_id, const string& name,
    const string& category, int64_t tid) {
  // These annotations cause TSAN to ignore the synchronization on lock_
  // without causing the subsequent mutations to be treated as data races
  // in and of themselves (that's what IGNORE_READS_AND_WRITES does).
  //
  // Why do we need them here and in SuperviseThread()? TSAN operates by
  // observing synchronization events and using them to establish "happens
  // before" relationships between threads. Where these relationships are
  // not built, shared state access constitutes a data race. The
  // synchronization events here, in RemoveThread(), and in
  // SuperviseThread() may cause TSAN to establish a "happens before"
  // relationship between thread functors, ignoring potential data races.
  // The annotations prevent this from happening.
  ANNOTATE_IGNORE_SYNC_BEGIN();
  ANNOTATE_IGNORE_READS_AND_WRITES_BEGIN();
  {
    MutexLock l(lock_);
    thread_categories_[category][pthread_id] = ThreadDescriptor(category, name, tid);
    if (metrics_enabled_) {
      threads_running_metric_++;
      threads_started_metric_++;
      started_category_tracker_->IncrementCategory(category);
      running_category_tracker_->IncrementCategory(category);
    }
  }
  ANNOTATE_IGNORE_SYNC_END();
  ANNOTATE_IGNORE_READS_AND_WRITES_END();
}

void ThreadMgr::RemoveThread(const pthread_t& pthread_id, const string& category) {
  ANNOTATE_IGNORE_SYNC_BEGIN();
  ANNOTATE_IGNORE_READS_AND_WRITES_BEGIN();
  {
    MutexLock l(lock_);
    auto category_it = thread_categories_.find(category);
    DCHECK(category_it != thread_categories_.end());
    category_it->second.erase(pthread_id);
    if (metrics_enabled_) {
      threads_running_metric_--;
      running_category_tracker_->DecrementCategory(category);
    }
  }
  ANNOTATE_IGNORE_SYNC_END();
  ANNOTATE_IGNORE_READS_AND_WRITES_END();
}

int Compare(const Result<StackTrace>& lhs, const Result<StackTrace>& rhs) {
  if (lhs.ok()) {
    if (!rhs.ok()) {
      return -1;
    }
    return lhs->compare(*rhs);
  }
  if (rhs.ok()) {
    return 1;
  }
  return lhs.status().message().compare(rhs.status().message());

}

void ThreadMgr::RenderThreadCategoryRows(const ThreadCategory& category, std::string* output) {
  struct ThreadData {
    int64_t tid = 0;
    ThreadIdForStack tid_for_stack = 0;
    const std::string* name = nullptr;
    ThreadStats stats;
    Result<StackTrace> stack_trace = StackTrace();
    int rowspan = -1;
  };
  std::vector<ThreadData> threads;
  std::vector<ThreadIdForStack> thread_ids;
  threads.resize(category.size());
  thread_ids.reserve(category.size());
  {
    auto* data = threads.data();
    for (const ThreadCategory::value_type& thread : category) {
      data->name = &thread.second.name();
      data->tid = thread.second.thread_id();
#if defined(__linux__)
      data->tid_for_stack = data->tid;
#else
      data->tid_for_stack = thread.first;
#endif
      Status status = GetThreadStats(data->tid, &data->stats);
      if (!status.ok()) {
        YB_LOG_EVERY_N(INFO, 100) << "Could not get per-thread statistics: "
                                  << status.ToString();
      }
      thread_ids.push_back(data->tid_for_stack);
      ++data;
    }
  }

  if (threads.empty()) {
    return;
  }

  std::sort(thread_ids.begin(), thread_ids.end());
  auto stacks = ThreadStacks(thread_ids);

  for (ThreadData& data : threads) {
    auto it = std::lower_bound(thread_ids.begin(), thread_ids.end(), data.tid_for_stack);
    DCHECK(it != thread_ids.end() && *it == data.tid_for_stack);
    data.stack_trace = stacks[it - thread_ids.begin()];
  }

  std::sort(threads.begin(), threads.end(), [](const ThreadData& lhs, const ThreadData& rhs) {
    return Compare(lhs.stack_trace, rhs.stack_trace) < 0;
  });

  auto it = threads.begin();
  auto first = it;
  first->rowspan = 1;
  while (++it != threads.end()) {
    if (Compare(it->stack_trace, first->stack_trace) != 0) {
      first = it;
      first->rowspan = 1;
    } else {
      ++first->rowspan;
    }
  }

  std::string* active_out = output;
  for (const auto& thread : threads) {
    std::string symbolized;
    if (thread.rowspan > 0) {
      StackTraceGroup group = StackTraceGroup::kActive;
      if (thread.stack_trace.ok()) {
        symbolized = thread.stack_trace->Symbolize(StackTraceLineFormat::DEFAULT, &group);
      } else {
        symbolized = thread.stack_trace.status().message().ToBuffer();
      }
      active_out = output + to_underlying(group);
    }

    *active_out += Format(
         "<tr><td>$0</td><td>$1</td><td>$2</td><td>$3</td>",
         *thread.name, MonoDelta::FromNanoseconds(thread.stats.user_ns),
         MonoDelta::FromNanoseconds(thread.stats.kernel_ns / 1e9),
         MonoDelta::FromNanoseconds(thread.stats.iowait_ns / 1e9));
    if (thread.rowspan > 0) {
      *active_out += Format("<td rowspan=\"$0\"><pre>$1\nTotal number of threads: $0</pre></td>",
                            thread.rowspan, symbolized);
    }
    *active_out += "</tr>\n";
  }
}

void ThreadMgr::RenderThreadGroup(const std::string& group, std::ostream& output) {
  output << "<h2>Thread Group: ";
  EscapeForHtml(group, &output);
  output << "</h2>" << endl;

  std::vector<const ThreadCategory*> categories_to_print;
  if (group != kAllGroups) {
    auto category = thread_categories_.find(group);
    if (category == thread_categories_.end()) {
      output << "Thread group '";
      EscapeForHtml(group, &output);
      output << "' not found" << endl;
      return;
    }
    categories_to_print.push_back(&category->second);
    output << "<h3>" << category->first << " : " << category->second.size() << "</h3>";
  } else {
    for (const ThreadCategoryMap::value_type& category : thread_categories_) {
      categories_to_print.push_back(&category.second);
    }
    output << "<h3>All Threads : </h3>";
  }

  output << "<table class='table table-hover table-border'>";
  output << "<tr><th>Thread name</th><th>Cumulative User CPU(s)</th>"
         << "<th>Cumulative Kernel CPU(s)</th>"
         << "<th>Cumulative IO-wait(s)</th></tr>";

  std::array<std::string, kStackTraceGroupMapSize> groups;

  for (const ThreadCategory* category : categories_to_print) {
    RenderThreadCategoryRows(*category, groups.data());
  }

  for (auto g : StackTraceGroupList()) {
    output << groups[to_underlying(g)];
  }
  output << "</table>";
}

void ThreadMgr::ThreadPathHandler(
    const WebCallbackRegistry::WebRequest& req,
    WebCallbackRegistry::WebResponse* resp) {
  auto& output = resp->output;
  MutexLock l(lock_);
  auto category_name = req.parsed_args.find("group");
  if (category_name != req.parsed_args.end()) {
    string group = EscapeForHtmlToString(category_name->second);
    RenderThreadGroup(group, output);
  } else {
    output << "<h2>Thread Groups</h2>";
    if (metrics_enabled_) {
      output << "<h4>" << threads_running_metric_ << " thread(s) running";
    }
    output << "<a href='/threadz?group=" << kAllGroups << "'><h3>All Threads</h3>";

    for (const ThreadCategoryMap::value_type& category : thread_categories_) {
      string category_arg;
      UrlEncode(category.first, &category_arg);
      output << "<a href='/threadz?group=" << category_arg << "'><h3>"
             << category.first << " : " << category.second.size() << "</h3></a>";
    }
  }
}

// Singleton instance of ThreadMgr. Only visible in this file, used only by Thread.
// The Thread class adds a reference to thread_manager while it is supervising a thread so
// that a race between the end of the process's main thread (and therefore the destruction
// of thread_manager) and the end of a thread that tries to remove itself from the
// manager after the destruction can be avoided.
shared_ptr<ThreadMgr> thread_manager;

// Controls the single (lazy) initialization of thread_manager.
std::once_flag init_threading_internal_once_flag;

void InitThreadingInternal() {
  // Warm up the stack trace library. This avoids a race in libunwind initialization
  // by making sure we initialize it before we start any other threads.
  GetStackTraceHex();
  thread_manager = std::make_shared<ThreadMgr>();
}

// Thread local prefix used in tests to display the daemon name.
std::string* AccessThreadFormattedLogPrefix() {
  BLOCK_STATIC_THREAD_LOCAL(std::string, log_prefix);
  return log_prefix;
}

std::string* AccessThreadUnformattedLogPrefix() {
  BLOCK_STATIC_THREAD_LOCAL(std::string, log_prefix_unformatted);
  return log_prefix_unformatted;
}

void TEST_FormatAndSetThreadLogPrefix(const std::string& new_prefix) {
  *AccessThreadUnformattedLogPrefix() = new_prefix;
  *AccessThreadFormattedLogPrefix() =
      new_prefix.empty() ? new_prefix : Format("[$0] ", new_prefix);
}

} // anonymous namespace

const char* TEST_GetThreadLogPrefix() {
  return AccessThreadFormattedLogPrefix()->c_str();
}

std::string TEST_GetThreadUnformattedLogPrefix() {
  return *AccessThreadUnformattedLogPrefix();
}

TEST_SetThreadPrefixScoped::TEST_SetThreadPrefixScoped(const std::string& prefix)
    : old_prefix_(*AccessThreadUnformattedLogPrefix()) {
  TEST_FormatAndSetThreadLogPrefix(
      Format("$0$1$2", old_prefix_, old_prefix_.empty() ? "" : "-", prefix));
}

TEST_SetThreadPrefixScoped::~TEST_SetThreadPrefixScoped() {
  TEST_FormatAndSetThreadLogPrefix(old_prefix_);
}

void SetThreadName(const std::string& name) {
#if defined(__linux__)
  // http://0pointer.de/blog/projects/name-your-threads.html
  // Set the name for the LWP (which gets truncated to 15 characters).
  // Note that glibc also has a 'pthread_setname_np' api, but it may not be
  // available everywhere and it's only benefit over using prctl directly is
  // that it can set the name of threads other than the current thread.
  int err = prctl(PR_SET_NAME, name.c_str());
#else
  int err = pthread_setname_np(name.c_str());
#endif // defined(__linux__)
  // We expect EPERM failures in sandboxed processes, just ignore those.
  if (err < 0 && errno != EPERM) {
    PLOG(ERROR) << "SetThreadName";
  }
}

void InitThreading() {
  std::call_once(init_threading_internal_once_flag, InitThreadingInternal);
}

__thread Thread* Thread::tls_ = nullptr;

Status StartThreadInstrumentation(const scoped_refptr<MetricEntity>& server_metrics,
                                  WebCallbackRegistry* web) {
  InitThreading();
  return thread_manager->StartInstrumentation(server_metrics, web);
}

ThreadJoiner::ThreadJoiner(Thread* thr)
  : thread_(CHECK_NOTNULL(thr)) {
}

ThreadJoiner& ThreadJoiner::warn_after(MonoDelta duration) {
  warn_after_ = duration;
  return *this;
}

ThreadJoiner& ThreadJoiner::warn_every(MonoDelta duration) {
  warn_every_ = duration;
  return *this;
}

ThreadJoiner& ThreadJoiner::give_up_after(MonoDelta duration) {
  give_up_after_ = duration;
  return *this;
}

Status ThreadJoiner::Join() {
  if (Thread::current_thread() &&
      Thread::current_thread()->tid() == thread_->tid()) {
    return STATUS(InvalidArgument, "Can't join on own thread", thread_->name_);
  }

  // Early exit: double join is a no-op.
  if (!thread_->joinable_) {
    return Status::OK();
  }

  MonoDelta waited = MonoDelta::kZero;
  bool keep_trying = true;
  int stack_trace_report_counter = -5;
  while (keep_trying) {
    if (waited >= warn_after_) {
      LOG(WARNING) << Format("Waited for $0 trying to join with $1 (tid $2)",
                             waited, thread_->name_, thread_->tid_);
      if (++stack_trace_report_counter == 0) {
        auto stack_trace = ThreadStack(thread_->tid_for_stack());
        if (stack_trace.ok()) {
          LOG(WARNING) << "Stack trace for " << thread_->tid_ << ":\n" << stack_trace->Symbolize();
        }
      }
    }

    auto remaining_before_giveup = give_up_after_;
    if (remaining_before_giveup != MonoDelta::kMax) {
      remaining_before_giveup -= waited;
    }

    auto remaining_before_next_warn = warn_every_;
    if (waited < warn_after_) {
      remaining_before_next_warn = warn_after_ - waited;
    }

    if (remaining_before_giveup < remaining_before_next_warn) {
      keep_trying = false;
    }

    auto wait_for = std::min(remaining_before_giveup, remaining_before_next_warn);

    if (thread_->done_.WaitFor(wait_for)) {
      // Unconditionally join before returning, to guarantee that any TLS
      // has been destroyed (pthread_key_create() destructors only run
      // after a pthread's user method has returned).
      int ret = pthread_join(thread_->thread_, NULL);
      CHECK_EQ(ret, 0);
      thread_->joinable_ = false;
      return Status::OK();
    }
    waited += wait_for;
  }

#ifndef NDEBUG
  LOG(WARNING) << "Failed to join:\n" << DumpThreadStack(thread_->tid_for_stack());
#endif

  return STATUS_FORMAT(Aborted, "Timed out after $0 joining on $1", waited, thread_->name_);
}

Thread::Thread(std::string category, std::string name, ThreadFunctor functor)
    : thread_(0),
      category_(std::move(category)),
      name_(std::move(name)),
      TEST_log_prefix_(*AccessThreadUnformattedLogPrefix()),
      tid_(CHILD_WAITING_TID),
      functor_(std::move(functor)),
      done_(1),
      joinable_(false) {}

Thread::~Thread() {
  if (joinable_) {
    int ret = pthread_detach(thread_);
    CHECK_EQ(ret, 0);
  }
}

void Thread::CallAtExit(const Closure& cb) {
  CHECK_EQ(Thread::current_thread(), this);
  exit_callbacks_.push_back(cb);
}

std::string Thread::ToString() const {
  return Substitute("Thread $0 (name: \"$1\", category: \"$2\")", tid_, name_, category_);
}

Status Thread::TryStartThread(Thread* t) {
  pthread_attr_t attr;
  RETURN_NOT_OK(STATUS_FROM_ERRNO_RV_FN_CALL(pthread_attr_init, &attr));
  if (FLAGS_min_thread_stack_size_bytes > 0) {
    RETURN_NOT_OK(STATUS_FROM_ERRNO_RV_FN_CALL(
        pthread_attr_setstacksize, &attr, FLAGS_min_thread_stack_size_bytes));
  }
  RETURN_NOT_OK(STATUS_FROM_ERRNO_RV_FN_CALL(
      pthread_create, &t->thread_, &attr, &Thread::SuperviseThread, t));
  return Status::OK();
}

Status Thread::StartThread(const std::string& category, const std::string& name,
                           ThreadFunctor functor, scoped_refptr<Thread> *holder) {
  InitThreading();
  string padded_name = name;
  // See comment in SetThreadName. We're padding names to the 15 char limit in order to get
  // aggregations when using the linux perf tool, as that groups up stacks based on the 15 char
  // prefix of all the thread names.
  if (name.length() < kMaxThreadNameInPerf) {
    padded_name += string(kMaxThreadNameInPerf - name.length(), Thread::kPaddingChar);
  }
  const string log_prefix = Substitute("$0 ($1) ", padded_name, category);
  SCOPED_LOG_SLOW_EXECUTION_PREFIX(WARNING, 500 /* ms */, log_prefix, "starting thread");

  // Temporary reference for the duration of this function.
  scoped_refptr<Thread> t(new Thread(category, padded_name, std::move(functor)));

  {
    SCOPED_LOG_SLOW_EXECUTION_PREFIX(WARNING, 500 /* ms */, log_prefix, "creating pthread");

    // Block stack trace collection while we create a thread. This also prevents stack trace
    // collection in the new thread while it is being started since it will inherit our signal
    // masks. SuperviseThread function will unblock the signal as soon as thread begins to run.
    const auto old_signal = VERIFY_RESULT(ThreadSignalMaskBlock({GetStackTraceSignal()}));
    const auto thread_start_status = TryStartThread(t.get());
    const auto mask_restore_status = ThreadSignalMaskRestore(old_signal);
    if (!thread_start_status.ok() || !mask_restore_status.ok()) {
      return STATUS_FORMAT(
          RuntimeError, "Failed to start thread. "
          "Thread start status: $0, signal mask restore status: $1",
          thread_start_status, mask_restore_status);
    }
  }

  // The thread has been created and is now joinable.
  //
  // Why set this in the parent and not the child? Because only the parent
  // (or someone communicating with the parent) can join, so joinable must
  // be set before the parent returns.
  t->joinable_ = true;

  // Optional, and only set if the thread was successfully created.
  if (holder) {
    *holder = t;
  }

  // The tid_ member goes through the following states:
  // 1  CHILD_WAITING_TID: the child has just been spawned and is waiting
  //    for the parent to finish writing to caller state (i.e. 'holder').
  // 2. PARENT_WAITING_TID: the parent has updated caller state and is now
  //    waiting for the child to write the tid.
  // 3. <value>: both the parent and the child are free to continue. If the
  //    value is INVALID_TID, the child could not discover its tid.
  Release_Store(&t->tid_, PARENT_WAITING_TID);
  {
    SCOPED_LOG_SLOW_EXECUTION_PREFIX(WARNING, 500 /* ms */, log_prefix,
                                     "waiting for new thread to publish its TID");
    int loop_count = 0;
    while (Acquire_Load(&t->tid_) == PARENT_WAITING_TID) {
      boost::detail::yield(loop_count++);
    }
  }

  VLOG(2) << "Started thread " << t->tid()<< " - " << category << ":" << padded_name;
  return Status::OK();
}

void* Thread::SuperviseThread(void* arg) {
  CHECK_OK(ThreadSignalMaskUnblock({GetStackTraceSignal()}));

  Thread* t = static_cast<Thread*>(arg);
  int64_t system_tid = Thread::CurrentThreadId();
  if (system_tid == -1) {
    string error_msg = ErrnoToString(errno);
    YB_LOG_EVERY_N(INFO, 100) << "Could not determine thread ID: " << error_msg;
  }
  TEST_FormatAndSetThreadLogPrefix(t->TEST_log_prefix_);
  string name = strings::Substitute("$0-$1", t->name(), system_tid);

  // Take an additional reference to the thread manager, which we'll need below.
  ANNOTATE_IGNORE_SYNC_BEGIN();
  shared_ptr<ThreadMgr> thread_mgr_ref = thread_manager;
  ANNOTATE_IGNORE_SYNC_END();

  // Set up the TLS.
  //
  // We could store a scoped_refptr in the TLS itself, but as its
  // lifecycle is poorly defined, we'll use a bare pointer and take an
  // additional reference on t out of band, in thread_ref.
  scoped_refptr<Thread> thread_ref = t;
  t->tls_ = t;

  // Wait until the parent has updated all caller-visible state, then write
  // the TID to 'tid_', thus completing the parent<-->child handshake.
  int loop_count = 0;
  while (Acquire_Load(&t->tid_) == CHILD_WAITING_TID) {
    boost::detail::yield(loop_count++);
  }
  Release_Store(&t->tid_, system_tid);

  thread_manager->SetThreadName(name, t->tid());
  thread_manager->AddThread(pthread_self(), name, t->category(), t->tid());

  cds::threading::Manager::attachThread();

  // FinishThread() is guaranteed to run (even if functor_ throws an
  // exception) because pthread_cleanup_push() creates a scoped object
  // whose destructor invokes the provided callback.
  pthread_cleanup_push(&Thread::FinishThread, t);
  t->functor_();
  pthread_cleanup_pop(true);

  return NULL;
}

void Thread::Join() {
  auto status = ThreadJoiner(this).Join();
  LOG_IF(DFATAL, !status.ok()) << "Thread join failed: " << status;
}

void Thread::FinishThread(void* arg) {
  cds::threading::Manager::detachThread();

  Thread* t = static_cast<Thread*>(arg);

  for (Closure& c : t->exit_callbacks_) {
    c.Run();
  }

  // We're here either because of the explicit pthread_cleanup_pop() in
  // SuperviseThread() or through pthread_exit(). In either case,
  // thread_manager is guaranteed to be live because thread_mgr_ref in
  // SuperviseThread() is still live.
  thread_manager->RemoveThread(pthread_self(), t->category());

  // Signal any Joiner that we're done.
  t->done_.CountDown();

  VLOG(2) << "Ended thread " << t->tid() << " - "
          << t->category() << ":" << t->name();

  // Its no longer safe to collect stack traces in this thread.
  CHECK_OK(ThreadSignalMaskBlock({GetStackTraceSignal()}));
}

Status Thread::SendSignal(ThreadIdForStack tid, int signal) {
  // We use the raw syscall here instead of kill() to ensure that we don't accidentally
  // send a signal to some other process in the case that the thread has exited and
  // the TID been recycled.
#if defined(__linux__)
  int res = narrow_cast<int>(syscall(SYS_tgkill, getpid(), tid, signal));
#else
  int res = pthread_kill(tid, signal);
#endif
  if (res != 0) {
    static const Status status = STATUS(
        RuntimeError, "Unable to deliver signal: process may have exited");
    return status;
  }
  return Status::OK();
}

CDSAttacher::CDSAttacher() {
  cds::threading::Manager::attachThread();
}

CDSAttacher::~CDSAttacher() {
  cds::threading::Manager::detachThread();
}

void RenderAllThreadStacks(std::ostream& output) {
  thread_manager->RenderThreadGroup(kAllGroups, output);
}

size_t CountManagedThreads() {
  return thread_manager->ReadThreadsRunning();
}

} // namespace yb
