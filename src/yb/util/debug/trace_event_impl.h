// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
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

#pragma once

#include <stdint.h>

#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <stack>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "yb/util/flags.h"
#include <gtest/gtest_prod.h>

#include "yb/gutil/atomicops.h"
#include "yb/gutil/callback.h"
#include "yb/gutil/integral_types.h"
#include "yb/gutil/ref_counted.h"
#include "yb/gutil/ref_counted_memory.h"
#include "yb/gutil/spinlock.h"
#include "yb/gutil/walltime.h"

#include "yb/util/mutex.h"
#include "yb/util/shared_lock.h"
#include "yb/util/threadlocal.h"

// Older style trace macros with explicit id and extra data
// Only these macros result in publishing data to ETW as currently implemented.
#define TRACE_EVENT_BEGIN_ETW(name, id, extra) \
    base::debug::TraceLog::AddTraceEventEtw( \
        TRACE_EVENT_PHASE_BEGIN, \
        name, reinterpret_cast<const void*>(id), extra)

#define TRACE_EVENT_END_ETW(name, id, extra) \
    base::debug::TraceLog::AddTraceEventEtw( \
        TRACE_EVENT_PHASE_END, \
        name, reinterpret_cast<const void*>(id), extra)

#define TRACE_EVENT_INSTANT_ETW(name, id, extra) \
    base::debug::TraceLog::AddTraceEventEtw( \
        TRACE_EVENT_PHASE_INSTANT, \
        name, reinterpret_cast<const void*>(id), extra)

template <typename Type>
class Singleton;

#if defined(COMPILER_GCC)
namespace BASE_HASH_NAMESPACE {
template <>
struct hash<yb::Thread*> {
  std::size_t operator()(yb::Thread* value) const {
    return reinterpret_cast<std::size_t>(value);
  }
};
}  // BASE_HASH_NAMESPACE
#endif

namespace yb {

class Thread;

namespace debug {

// For any argument of type TRACE_VALUE_TYPE_CONVERTABLE the provided
// class must implement this interface.
class ConvertableToTraceFormat : public yb::RefCountedThreadSafe<ConvertableToTraceFormat> {
 public:
  // Append the class info to the provided |out| string. The appended
  // data must be a valid JSON object. Strings must be properly quoted, and
  // escaped. There is no processing applied to the content after it is
  // appended.
  virtual void AppendAsTraceFormat(std::string* out) const = 0;

 protected:
  virtual ~ConvertableToTraceFormat() {}

 private:
  friend class yb::RefCountedThreadSafe<ConvertableToTraceFormat>;
};

struct TraceEventHandle {
  uint32 chunk_seq = 0;
  uint16 chunk_index = 0;
  uint16 event_index = 0;
};

const int kTraceMaxNumArgs = 2;

class BASE_EXPORT TraceEvent {
 public:
  union TraceValue {
    bool as_bool;
    uint64_t as_uint;
    long long as_int;  // NOLINT(runtime/int)
    double as_double;
    const void* as_pointer;
    const char* as_string;
  };

  TraceEvent();
  ~TraceEvent();

  // We don't need to copy TraceEvent except when TraceEventBuffer is cloned.
  // Use explicit copy method to avoid accidentally misuse of copy.
  void CopyFrom(const TraceEvent& other);

  void Initialize(
      int64_t thread_id,
      MicrosecondsInt64 timestamp,
      MicrosecondsInt64 thread_timestamp,
      char phase,
      const unsigned char* category_group_enabled,
      const char* name,
      uint64_t id,
      int num_args,
      const char** arg_names,
      const unsigned char* arg_types,
      const uint64_t* arg_values,
      const scoped_refptr<ConvertableToTraceFormat>* convertable_values,
      unsigned char flags);

  void Reset();

  void UpdateDuration(const MicrosecondsInt64& now, const MicrosecondsInt64& thread_now);

  // Serialize event data to JSON
  void AppendAsJSON(std::string* out) const;
  void AppendPrettyPrinted(std::ostringstream* out) const;

  static void AppendValueAsJSON(unsigned char type,
                                TraceValue value,
                                std::string* out);

  MicrosecondsInt64 timestamp() const { return timestamp_; }
  MicrosecondsInt64 thread_timestamp() const { return thread_timestamp_; }
  char phase() const { return phase_; }
  int64_t thread_id() const { return thread_id_; }
  MicrosecondsInt64 duration() const { return duration_; }
  MicrosecondsInt64 thread_duration() const { return thread_duration_; }
  uint64_t id() const { return id_; }
  unsigned char flags() const { return flags_; }

  // Exposed for unittesting:

  const yb::RefCountedString* parameter_copy_storage() const {
    return parameter_copy_storage_.get();
  }

  const unsigned char* category_group_enabled() const {
    return category_group_enabled_;
  }

  const char* name() const { return name_; }

#if defined(OS_ANDROID)
  void SendToATrace();
#endif

 private:
  // Note: these are ordered by size (largest first) for optimal packing.
  MicrosecondsInt64 timestamp_;
  MicrosecondsInt64 thread_timestamp_;
  MicrosecondsInt64 duration_;
  MicrosecondsInt64 thread_duration_;
  // id_ can be used to store phase-specific data.
  uint64_t id_;
  TraceValue arg_values_[kTraceMaxNumArgs];
  const char* arg_names_[kTraceMaxNumArgs];
  scoped_refptr<ConvertableToTraceFormat> convertable_values_[kTraceMaxNumArgs];
  const unsigned char* category_group_enabled_;
  const char* name_;
  scoped_refptr<yb::RefCountedString> parameter_copy_storage_;
  int64_t thread_id_;
  char phase_;
  unsigned char flags_;
  unsigned char arg_types_[kTraceMaxNumArgs];

  DISALLOW_COPY_AND_ASSIGN(TraceEvent);
};

// TraceBufferChunk is the basic unit of TraceBuffer.
class BASE_EXPORT TraceBufferChunk {
 public:
  explicit TraceBufferChunk(uint32 seq)
      : next_free_(0),
        seq_(seq) {
  }

  void Reset(uint32 new_seq);
  TraceEvent* AddTraceEvent(size_t* event_index);
  bool IsFull() const { return next_free_ == kTraceBufferChunkSize; }

  uint32 seq() const { return seq_; }
  size_t capacity() const { return kTraceBufferChunkSize; }
  size_t size() const { return next_free_; }

  TraceEvent* GetEventAt(size_t index) {
    DCHECK(index < size());
    return &chunk_[index];
  }
  const TraceEvent* GetEventAt(size_t index) const {
    DCHECK(index < size());
    return &chunk_[index];
  }

  std::unique_ptr<TraceBufferChunk> Clone() const;

  static const size_t kTraceBufferChunkSize = 64;

 private:
  size_t next_free_;
  TraceEvent chunk_[kTraceBufferChunkSize];
  uint32 seq_;
};

// TraceBuffer holds the events as they are collected.
class BASE_EXPORT TraceBuffer {
 public:
  virtual ~TraceBuffer() {}

  virtual std::unique_ptr<TraceBufferChunk> GetChunk(size_t *index) = 0;
  virtual void ReturnChunk(size_t index,
                           std::unique_ptr<TraceBufferChunk> chunk) = 0;

  virtual bool IsFull() const = 0;
  virtual size_t Size() const = 0;
  virtual size_t Capacity() const = 0;
  virtual TraceEvent* GetEventByHandle(TraceEventHandle handle) = 0;

  // For iteration. Each TraceBuffer can only be iterated once.
  virtual const TraceBufferChunk* NextChunk() = 0;

  virtual std::unique_ptr<TraceBuffer> CloneForIteration() const = 0;
};

// TraceResultBuffer collects and converts trace fragments returned by TraceLog
// to JSON output.
class TraceResultBuffer {
 public:
  static std::string FlushTraceLogToString();
  static std::string FlushTraceLogToStringButLeaveBufferIntact();

 private:
  TraceResultBuffer();
  ~TraceResultBuffer();

  static std::string DoFlush(bool leave_intact);

  // Callback for TraceLog::Flush
  void Collect(const scoped_refptr<RefCountedString>& s,
               bool has_more_events);

  bool first_;
  std::string json_;
};

class BASE_EXPORT CategoryFilter {
 public:
  typedef std::vector<std::string> StringList;

  // The default category filter, used when none is provided.
  // Allows all categories through, except if they end in the suffix 'Debug' or
  // 'Test'.
  static const char* kDefaultCategoryFilterString;

  // |filter_string| is a comma-delimited list of category wildcards.
  // A category can have an optional '-' prefix to make it an excluded category.
  // All the same rules apply above, so for example, having both included and
  // excluded categories in the same list would not be supported.
  //
  // Example: CategoryFilter"test_MyTest*");
  // Example: CategoryFilter("test_MyTest*,test_OtherStuff");
  // Example: CategoryFilter("-excluded_category1,-excluded_category2");
  // Example: CategoryFilter("-*,webkit"); would disable everything but webkit.
  // Example: CategoryFilter("-webkit"); would enable everything but webkit.
  //
  // Category filters can also be used to configure synthetic delays.
  //
  // Example: CategoryFilter("DELAY(gpu.PresentingFrame;16)"); would make swap
  //          buffers always take at least 16 ms.
  // Example: CategoryFilter("DELAY(gpu.PresentingFrame;16;oneshot)"); would
  //          make swap buffers take at least 16 ms the first time it is
  //          called.
  // Example: CategoryFilter("DELAY(gpu.PresentingFrame;16;alternating)");
  //          would make swap buffers take at least 16 ms every other time it
  //          is called.
  explicit CategoryFilter(const std::string& filter_string);

  CategoryFilter(const CategoryFilter& cf);

  ~CategoryFilter();

  CategoryFilter& operator=(const CategoryFilter& rhs);

  // Writes the string representation of the CategoryFilter. This is a comma
  // separated string, similar in nature to the one used to determine
  // enabled/disabled category patterns, except here there is an arbitrary
  // order, included categories go first, then excluded categories. Excluded
  // categories are distinguished from included categories by the prefix '-'.
  std::string ToString() const;

  // Determines whether category group would be enabled or
  // disabled by this category filter.
  bool IsCategoryGroupEnabled(const char* category_group) const;

  // Return a list of the synthetic delays specified in this category filter.
  const StringList& GetSyntheticDelayValues() const;

  // Merges nested_filter with the current CategoryFilter
  void Merge(const CategoryFilter& nested_filter);

  // Clears both included/excluded pattern lists. This would be equivalent to
  // creating a CategoryFilter with an empty string, through the constructor.
  // i.e: CategoryFilter("").
  //
  // When using an empty filter, all categories are considered included as we
  // are not excluding anything.
  void Clear();

 private:
  FRIEND_TEST(TraceEventTestFixture, CategoryFilter);

  static bool IsEmptyOrContainsLeadingOrTrailingWhitespace(
      const std::string& str);

  void Initialize(const std::string& filter_string);
  void WriteString(const StringList& values,
                   std::string* out,
                   bool included) const;
  void WriteString(const StringList& delays, std::string* out) const;
  bool HasIncludedPatterns() const;

  bool DoesCategoryGroupContainCategory(const char* category_group,
                                        const char* category) const;

  StringList included_;
  StringList disabled_;
  StringList excluded_;
  StringList delays_;
};

class TraceSamplingThread;

class BASE_EXPORT TraceLog {
 public:
  enum Mode {
    DISABLED = 0,
    RECORDING_MODE,
    MONITORING_MODE,
  };

  // Options determines how the trace buffer stores data.
  enum Options {
    // Record until the trace buffer is full.
    RECORD_UNTIL_FULL = 1 << 0,

    // Record until the user ends the trace. The trace buffer is a fixed size
    // and we use it as a ring buffer during recording.
    RECORD_CONTINUOUSLY = 1 << 1,

    // Enable the sampling profiler in the recording mode.
    ENABLE_SAMPLING = 1 << 2,

    // Echo to console. Events are discarded.
    ECHO_TO_CONSOLE = 1 << 3,
  };

  // The pointer returned from GetCategoryGroupEnabledInternal() points to a
  // value with zero or more of the following bits. Used in this class only.
  // The TRACE_EVENT macros should only use the value as a bool.
  // These values must be in sync with macro values in TraceEvent.h in Blink.
  enum CategoryGroupEnabledFlags {
    // Category group enabled for the recording mode.
    ENABLED_FOR_RECORDING = 1 << 0,
    // Category group enabled for the monitoring mode.
    ENABLED_FOR_MONITORING = 1 << 1,
    // Category group enabled by SetEventCallbackEnabled().
    ENABLED_FOR_EVENT_CALLBACK = 1 << 2,
  };

  static TraceLog* GetInstance();

  // Get set of known category groups. This can change as new code paths are
  // reached. The known category groups are inserted into |category_groups|.
  void GetKnownCategoryGroups(std::vector<std::string>* category_groups);

  // Retrieves a copy (for thread-safety) of the current CategoryFilter.
  CategoryFilter GetCurrentCategoryFilter();

  Options trace_options() const {
    return static_cast<Options>(base::subtle::NoBarrier_Load(&trace_options_));
  }

  // Enables normal tracing (recording trace events in the trace buffer).
  // See CategoryFilter comments for details on how to control what categories
  // will be traced. If tracing has already been enabled, |category_filter| will
  // be merged into the current category filter.
  void SetEnabled(const CategoryFilter& category_filter,
                  Mode mode, Options options);

  // Disables normal tracing for all categories.
  void SetDisabled();

  bool IsEnabled() { return mode_ != DISABLED; }

  // The number of times we have begun recording traces. If tracing is off,
  // returns -1. If tracing is on, then it returns the number of times we have
  // recorded a trace. By watching for this number to increment, you can
  // passively discover when a new trace has begun. This is then used to
  // implement the TRACE_EVENT_IS_NEW_TRACE() primitive.
  int GetNumTracesRecorded();

#if defined(OS_ANDROID)
  void StartATrace();
  void StopATrace();
  void AddClockSyncMetadataEvent();
#endif

  // Enabled state listeners give a callback when tracing is enabled or
  // disabled. This can be used to tie into other library's tracing systems
  // on-demand.
  class EnabledStateObserver {
   public:
    virtual ~EnabledStateObserver();

    // Called just after the tracing system becomes enabled, outside of the
    // |lock_|. TraceLog::IsEnabled() is true at this point.
    virtual void OnTraceLogEnabled() = 0;

    // Called just after the tracing system disables, outside of the |lock_|.
    // TraceLog::IsEnabled() is false at this point.
    virtual void OnTraceLogDisabled() = 0;
  };
  void AddEnabledStateObserver(EnabledStateObserver* listener);
  void RemoveEnabledStateObserver(EnabledStateObserver* listener);
  bool HasEnabledStateObserver(EnabledStateObserver* listener) const;

  float GetBufferPercentFull() const;
  bool BufferIsFull() const;

  // Not using yb::Callback because of its limited by 7 parameters.
  // Also, using primitive type allows directly passing callback from WebCore.
  // WARNING: It is possible for the previously set callback to be called
  // after a call to SetEventCallbackEnabled() that replaces or a call to
  // SetEventCallbackDisabled() that disables the callback.
  // This callback may be invoked on any thread.
  // For TRACE_EVENT_PHASE_COMPLETE events, the client will still receive pairs
  // of TRACE_EVENT_PHASE_BEGIN and TRACE_EVENT_PHASE_END events to keep the
  // interface simple.
  typedef void (*EventCallback)(MicrosecondsInt64 timestamp,
                                char phase,
                                const unsigned char* category_group_enabled,
                                const char* name,
                                uint64_t id,
                                int num_args,
                                const char* const arg_names[],
                                const unsigned char arg_types[],
                                const uint64_t arg_values[],
                                unsigned char flags);

  // Enable tracing for EventCallback.
  void SetEventCallbackEnabled(const CategoryFilter& category_filter,
                               EventCallback cb);
  void SetEventCallbackDisabled();

  // Flush all collected events to the given output callback. The callback will
  // be called one or more times synchronously from
  // the current thread with IPC-bite-size chunks. The string format is
  // undefined. Use TraceResultBuffer to convert one or more trace strings to
  // JSON. The callback can be null if the caller doesn't want any data.
  // Due to the implementation of thread-local buffers, flush can't be
  // done when tracing is enabled. If called when tracing is enabled, the
  // callback will be called directly with (empty_string, false) to indicate
  // the end of this unsuccessful flush.
  typedef yb::Callback<void(const scoped_refptr<yb::RefCountedString>&,
                              bool has_more_events)> OutputCallback;
  void Flush(const OutputCallback& cb);
  void FlushButLeaveBufferIntact(const OutputCallback& flush_output_callback);

  // Called by TRACE_EVENT* macros, don't call this directly.
  // The name parameter is a category group for example:
  // TRACE_EVENT0("renderer,webkit", "WebViewImpl::HandleInputEvent")
  static const unsigned char* GetCategoryGroupEnabled(const char* name);
  static const char* GetCategoryGroupName(
      const unsigned char* category_group_enabled);

  // Called by TRACE_EVENT* macros, don't call this directly.
  // If |copy| is set, |name|, |arg_name1| and |arg_name2| will be deep copied
  // into the event; see "Memory scoping note" and TRACE_EVENT_COPY_XXX above.
  TraceEventHandle AddTraceEvent(
      char phase,
      const unsigned char* category_group_enabled,
      const char* name,
      uint64_t id,
      int num_args,
      const char** arg_names,
      const unsigned char* arg_types,
      const uint64_t* arg_values,
      const scoped_refptr<ConvertableToTraceFormat>* convertable_values,
      unsigned char flags);
  TraceEventHandle AddTraceEventWithThreadIdAndTimestamp(
      char phase,
      const unsigned char* category_group_enabled,
      const char* name,
      uint64_t id,
      int64_t thread_id,
      const MicrosecondsInt64& timestamp,
      int num_args,
      const char** arg_names,
      const unsigned char* arg_types,
      const uint64_t* arg_values,
      const scoped_refptr<ConvertableToTraceFormat>* convertable_values,
      unsigned char flags);
  static void AddTraceEventEtw(char phase,
                               const char* category_group,
                               const void* id,
                               const char* extra);
  static void AddTraceEventEtw(char phase,
                               const char* category_group,
                               const void* id,
                               const std::string& extra);

  void UpdateTraceEventDuration(const unsigned char* category_group_enabled,
                                const char* name,
                                TraceEventHandle handle);

  // For every matching event, the callback will be called.
  typedef yb::Callback<void()> WatchEventCallback;
  void SetWatchEvent(const std::string& category_name,
                     const std::string& event_name,
                     const WatchEventCallback& callback);
  // Cancel the watch event. If tracing is enabled, this may race with the
  // watch event notification firing.
  void CancelWatchEvent();

  int process_id() const { return process_id_; }

  // Allow tests to inspect TraceEvents.
  size_t GetEventsSize() const { return logged_events_->Size(); }
  TraceEvent* GetEventByHandle(TraceEventHandle handle);

  void SetProcessID(int process_id);

  // Process sort indices, if set, override the order of a process will appear
  // relative to other processes in the trace viewer. Processes are sorted first
  // on their sort index, ascending, then by their name, and then tid.
  void SetProcessSortIndex(int sort_index);

  // Sets the name of the process.
  void SetProcessName(const std::string& process_name);

  // Processes can have labels in addition to their names. Use labels, for
  // instance, to list out the web page titles that a process is handling.
  void UpdateProcessLabel(int label_id, const std::string& current_label);
  void RemoveProcessLabel(int label_id);

  // Thread sort indices, if set, override the order of a thread will appear
  // within its process in the trace viewer. Threads are sorted first on their
  // sort index, ascending, then by their name, and then tid.
  void SetThreadSortIndex(int64_t tid , int sort_index);

  // Allow setting an offset between the current MicrosecondsInt64 time and the time
  // that should be reported.
  void SetTimeOffset(MicrosecondsInt64 offset);

  size_t GetObserverCountForTest() const;


 private:
  FRIEND_TEST(TraceEventTestFixture,
                           TraceBufferRingBufferGetReturnChunk);
  FRIEND_TEST(TraceEventTestFixture,
                           TraceBufferRingBufferHalfIteration);
  FRIEND_TEST(TraceEventTestFixture,
                           TraceBufferRingBufferFullIteration);

  // This allows constructor and destructor to be private and usable only
  // by the Singleton class.
  friend class Singleton<TraceLog>;

  // Enable/disable each category group based on the current mode_,
  // category_filter_, event_callback_ and event_callback_category_filter_.
  // Enable the category group in the enabled mode if category_filter_ matches
  // the category group, or event_callback_ is not null and
  // event_callback_category_filter_ matches the category group.
  void UpdateCategoryGroupEnabledFlags();
  void UpdateCategoryGroupEnabledFlag(AtomicWord category_index);

  // Configure synthetic delays based on the values set in the current
  // category filter.
  void UpdateSyntheticDelaysFromCategoryFilter();

  struct PerThreadInfo;
  class OptionalAutoLock;
  class ThreadLocalEventBuffer;

  TraceLog();
  ~TraceLog();
  const unsigned char* GetCategoryGroupEnabledInternal(const char* name);
  void AddMetadataEventsWhileLocked();

  TraceBuffer* trace_buffer() const { return logged_events_.get(); }
  TraceBuffer* CreateTraceBuffer();

  std::string EventToConsoleMessage(unsigned char phase,
                                    const MicrosecondsInt64& timestamp,
                                    TraceEvent* trace_event);

  TraceEvent* AddEventToThreadSharedChunkWhileLocked(TraceEventHandle* handle,
                                                     bool check_buffer_is_full);
  void CheckIfBufferIsFullWhileLocked();
  void SetDisabledWhileLocked();

  TraceEvent* GetEventByHandleInternal(TraceEventHandle handle,
                                       OptionalAutoLock* lock);

  void ConvertTraceEventsToTraceFormat(std::unique_ptr<TraceBuffer> logged_events,
                                       const OutputCallback& flush_output_callback);
  void FinishFlush(int generation,
                   const OutputCallback& flush_output_callback);

  // Called when a thread which has registered trace events is about to exit.
  void ThreadExiting();

  int generation() const {
    return static_cast<int>(base::subtle::NoBarrier_Load(&generation_));
  }
  bool CheckGeneration(int generation) const {
    return generation == this->generation();
  }
  void UseNextTraceBuffer();

  MicrosecondsInt64 OffsetNow() const {
    return OffsetTimestamp(GetMonoTimeMicros());
  }
  MicrosecondsInt64 OffsetTimestamp(const MicrosecondsInt64& timestamp) const {
    return timestamp - time_offset_;
  }

  // Create a new PerThreadInfo object for the current thread,
  // and register it in the active_threads_ list.
  PerThreadInfo* SetupThreadLocalBuffer();

  // This lock protects TraceLog member accesses (except for members protected
  // by thread_info_lock_) from arbitrary threads.
  mutable base::SpinLock lock_;
  // This lock protects accesses to thread_names_, thread_event_start_times_
  // and thread_colors_.
  base::SpinLock thread_info_lock_;
  int locked_line_;
  Mode mode_;
  int num_traces_recorded_;
  std::unique_ptr<TraceBuffer> logged_events_;
  AtomicWord /* EventCallback */ event_callback_;
  bool dispatching_to_observer_list_;
  std::vector<EnabledStateObserver*> enabled_state_observer_list_;

  std::string process_name_;
  std::unordered_map<int, std::string> process_labels_;
  int process_sort_index_;
  std::unordered_map<int, int> thread_sort_indices_;
  std::unordered_map<int64_t, std::string> thread_names_;

  // The following two maps are used only when ECHO_TO_CONSOLE.
  std::unordered_map<int64_t, std::stack<MicrosecondsInt64> > thread_event_start_times_;
  std::unordered_map<std::string, int> thread_colors_;

  // XORed with TraceID to make it unlikely to collide with other processes.
  uint64_t process_id_hash_;

  int process_id_;

  MicrosecondsInt64 time_offset_;

  // Allow tests to wake up when certain events occur.
  WatchEventCallback watch_event_callback_;
  AtomicWord /* const unsigned char* */ watch_category_;
  std::string watch_event_name_;

  AtomicWord /* Options */ trace_options_;

  // Sampling thread handles.
  std::unique_ptr<TraceSamplingThread> sampling_thread_;
  scoped_refptr<Thread> sampling_thread_handle_;

  CategoryFilter category_filter_;
  CategoryFilter event_callback_category_filter_;

  struct PerThreadInfo {
    ThreadLocalEventBuffer* event_buffer_;
    base::subtle::Atomic32 is_in_trace_event_;

    // Atomically take the event_buffer_ member, setting it to NULL.
    // Returns the old value of the member.
    ThreadLocalEventBuffer* AtomicTakeBuffer();
  };
  static __thread PerThreadInfo* thread_local_info_;

  Mutex active_threads_lock_;
  // Map of PID -> PerThreadInfo
  // Protected by active_threads_lock_.
  typedef std::unordered_map<int64_t, PerThreadInfo*> ActiveThreadMap;
  ActiveThreadMap active_threads_;

  // For events which can't be added into the thread local buffer, e.g. events
  // from threads without a message loop.
  std::unique_ptr<TraceBufferChunk> thread_shared_chunk_;
  size_t thread_shared_chunk_index_;

  // The generation is incremented whenever tracing is enabled, and incremented
  // again when the buffers are flushed. This ensures that trace events logged
  // for a previous tracing session do not get accidentally flushed in the
  // next tracing session.
  AtomicWord generation_;

  DISALLOW_COPY_AND_ASSIGN(TraceLog);
};

extern std::atomic<bool> trace_events_enabled;

inline bool TraceEventsEnabled() {
  return trace_events_enabled.load(std::memory_order_relaxed);
}

void EnableTraceEvents();

}  // namespace debug
}  // namespace yb
