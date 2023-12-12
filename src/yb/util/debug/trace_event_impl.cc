// Copyright (c) 2012 The Chromium Authors. All rights reserved.
//
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

#include "yb/util/debug/trace_event_impl.h"

#include <algorithm>
#include <list>
#include <vector>

#include "yb/util/logging.h"

#include "yb/gutil/bind.h"
#include "yb/gutil/dynamic_annotations.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/mathlimits.h"
#include "yb/gutil/singleton.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/stringprintf.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/split.h"
#include "yb/gutil/strings/util.h"
#include "yb/gutil/sysinfo.h"
#include "yb/gutil/walltime.h"

#include "yb/util/atomic.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/debug/trace_event_synthetic_delay.h"
#include "yb/util/flags.h"
#include "yb/util/jsonwriter.h"
#include "yb/util/status_fwd.h"
#include "yb/util/thread.h"

DEFINE_UNKNOWN_string(trace_to_console, "",
              "Trace pattern specifying which trace events should be dumped "
              "directly to the console");
TAG_FLAG(trace_to_console, experimental);

// The thread buckets for the sampling profiler.
BASE_EXPORT TRACE_EVENT_API_ATOMIC_WORD g_trace_state[3];

namespace yb {
namespace debug {

using base::SpinLockHolder;

using strings::SubstituteAndAppend;
using std::string;
using std::vector;

// This is used to avoid generating trace events during global initialization. If we allow that to
// happen, it may lead to segfaults (https://github.com/yugabyte/yugabyte-db/issues/11033).
std::atomic<bool> trace_events_enabled{false};

__thread TraceLog::PerThreadInfo* TraceLog::thread_local_info_ = nullptr;

namespace {

// Controls the number of trace events we will buffer in-memory
// before throwing them away.
const size_t kTraceBufferChunkSize = TraceBufferChunk::kTraceBufferChunkSize;
const size_t kTraceEventVectorBufferChunks = 256000 / kTraceBufferChunkSize;
const size_t kTraceEventRingBufferChunks = kTraceEventVectorBufferChunks / 4;
const size_t kTraceEventBatchChunks = 1000 / kTraceBufferChunkSize;
// Can store results for 30 seconds with 1 ms sampling interval.
const size_t kMonitorTraceEventBufferChunks = 30000 / kTraceBufferChunkSize;
// ECHO_TO_CONSOLE needs a small buffer to hold the unfinished COMPLETE events.
const size_t kEchoToConsoleTraceEventBufferChunks = 256;

const char kSyntheticDelayCategoryFilterPrefix[] = "DELAY(";

#define MAX_CATEGORY_GROUPS 100

// Parallel arrays g_category_groups and g_category_group_enabled are separate
// so that a pointer to a member of g_category_group_enabled can be easily
// converted to an index into g_category_groups. This allows macros to deal
// only with char enabled pointers from g_category_group_enabled, and we can
// convert internally to determine the category name from the char enabled
// pointer.
const char* g_category_groups[MAX_CATEGORY_GROUPS] = {
  "toplevel",
  "tracing already shutdown",
  "tracing categories exhausted; must increase MAX_CATEGORY_GROUPS",
  "__metadata"};

// The enabled flag is char instead of bool so that the API can be used from C.
unsigned char g_category_group_enabled[MAX_CATEGORY_GROUPS] = { 0 };
// Indexes here have to match the g_category_groups array indexes above.
const int g_category_already_shutdown = 1;
const int g_category_categories_exhausted = 2;
const int g_category_metadata = 3;
const int g_num_builtin_categories = 4;
// Skip default categories.
AtomicWord g_category_index = g_num_builtin_categories;

// The name of the current thread. This is used to decide if the current
// thread name has changed. We combine all the seen thread names into the
// output name for the thread.
__thread const char* g_current_thread_name = "";

static void NOTIMPLEMENTED() {
  LOG(FATAL);
}

class TraceBufferRingBuffer : public TraceBuffer {
 public:
  explicit TraceBufferRingBuffer(size_t max_chunks)
      : max_chunks_(max_chunks),
        recyclable_chunks_queue_(new size_t[queue_capacity()]),
        queue_head_(0),
        queue_tail_(max_chunks),
        current_iteration_index_(0),
        current_chunk_seq_(1) {
    chunks_.reserve(max_chunks);
    for (size_t i = 0; i < max_chunks; ++i)
      recyclable_chunks_queue_[i] = i;
  }

  ~TraceBufferRingBuffer() {
    STLDeleteElements(&chunks_);
  }

  std::unique_ptr<TraceBufferChunk> GetChunk(size_t* index) override {
    // Because the number of threads is much less than the number of chunks,
    // the queue should never be empty.
    DCHECK(!QueueIsEmpty());

    *index = recyclable_chunks_queue_[queue_head_];
    queue_head_ = NextQueueIndex(queue_head_);
    current_iteration_index_ = queue_head_;

    if (*index >= chunks_.size())
      chunks_.resize(*index + 1);

    TraceBufferChunk* chunk = chunks_[*index];
    chunks_[*index] = nullptr;  // Put NULL in the slot of a in-flight chunk.
    if (chunk)
      chunk->Reset(current_chunk_seq_++);
    else
      chunk = new TraceBufferChunk(current_chunk_seq_++);

    return std::unique_ptr<TraceBufferChunk>(chunk);
  }

  virtual void ReturnChunk(size_t index,
                           std::unique_ptr<TraceBufferChunk> chunk) override {
    // When this method is called, the queue should not be full because it
    // can contain all chunks including the one to be returned.
    DCHECK(!QueueIsFull());
    DCHECK(chunk);
    DCHECK_LT(index, chunks_.size());
    DCHECK(!chunks_[index]);
    chunks_[index] = chunk.release();
    recyclable_chunks_queue_[queue_tail_] = index;
    queue_tail_ = NextQueueIndex(queue_tail_);
  }

  bool IsFull() const override {
    return false;
  }

  size_t Size() const override {
    // This is approximate because not all of the chunks are full.
    return chunks_.size() * kTraceBufferChunkSize;
  }

  size_t Capacity() const override {
    return max_chunks_ * kTraceBufferChunkSize;
  }

  TraceEvent* GetEventByHandle(TraceEventHandle handle) override {
    if (handle.chunk_index >= chunks_.size())
      return nullptr;
    TraceBufferChunk* chunk = chunks_[handle.chunk_index];
    if (!chunk || chunk->seq() != handle.chunk_seq)
      return nullptr;
    return chunk->GetEventAt(handle.event_index);
  }

  const TraceBufferChunk* NextChunk() override {
    if (chunks_.empty())
      return nullptr;

    while (current_iteration_index_ != queue_tail_) {
      size_t chunk_index = recyclable_chunks_queue_[current_iteration_index_];
      current_iteration_index_ = NextQueueIndex(current_iteration_index_);
      if (chunk_index >= chunks_.size()) // Skip uninitialized chunks.
        continue;
      DCHECK(chunks_[chunk_index]);
      return chunks_[chunk_index];
    }
    return nullptr;
  }

  std::unique_ptr<TraceBuffer> CloneForIteration() const override {
    std::unique_ptr<ClonedTraceBuffer> cloned_buffer(new ClonedTraceBuffer());
    for (size_t queue_index = queue_head_; queue_index != queue_tail_;
        queue_index = NextQueueIndex(queue_index)) {
      size_t chunk_index = recyclable_chunks_queue_[queue_index];
      if (chunk_index >= chunks_.size()) // Skip uninitialized chunks.
        continue;
      TraceBufferChunk* chunk = chunks_[chunk_index];
      cloned_buffer->chunks_.push_back(chunk ? chunk->Clone().release() : nullptr);
    }
    return cloned_buffer;
  }

 private:
  class ClonedTraceBuffer : public TraceBuffer {
   public:
    ClonedTraceBuffer() : current_iteration_index_(0) {}
    ~ClonedTraceBuffer() {
      STLDeleteElements(&chunks_);
    }

    // The only implemented method.
    const TraceBufferChunk* NextChunk() override {
      return current_iteration_index_ < chunks_.size() ?
          chunks_[current_iteration_index_++] : nullptr;
    }

    std::unique_ptr<TraceBufferChunk> GetChunk(size_t* index) override {
      NOTIMPLEMENTED();
      return std::unique_ptr<TraceBufferChunk>();
    }
    virtual void ReturnChunk(size_t index,
                             std::unique_ptr<TraceBufferChunk>) override {
      NOTIMPLEMENTED();
    }
    bool IsFull() const override { return false; }
    size_t Size() const override { return 0; }
    size_t Capacity() const override { return 0; }
    TraceEvent* GetEventByHandle(TraceEventHandle handle) override {
      return nullptr;
    }
    std::unique_ptr<TraceBuffer> CloneForIteration() const override {
      NOTIMPLEMENTED();
      return std::unique_ptr<TraceBuffer>();
    }

    size_t current_iteration_index_;
    vector<TraceBufferChunk*> chunks_;
  };

  bool QueueIsEmpty() const {
    return queue_head_ == queue_tail_;
  }

  size_t QueueSize() const {
    return queue_tail_ > queue_head_ ? queue_tail_ - queue_head_ :
        queue_tail_ + queue_capacity() - queue_head_;
  }

  bool QueueIsFull() const {
    return QueueSize() == queue_capacity() - 1;
  }

  size_t queue_capacity() const {
    // One extra space to help distinguish full state and empty state.
    return max_chunks_ + 1;
  }

  size_t NextQueueIndex(size_t index) const {
    index++;
    if (index >= queue_capacity())
      index = 0;
    return index;
  }

  size_t max_chunks_;
  vector<TraceBufferChunk*> chunks_;

  std::unique_ptr<size_t[]> recyclable_chunks_queue_;
  size_t queue_head_;
  size_t queue_tail_;

  size_t current_iteration_index_;
  uint32 current_chunk_seq_;

  DISALLOW_COPY_AND_ASSIGN(TraceBufferRingBuffer);
};

class TraceBufferVector : public TraceBuffer {
 public:
  TraceBufferVector()
      : in_flight_chunk_count_(0),
        current_iteration_index_(0) {
    chunks_.reserve(kTraceEventVectorBufferChunks);
  }
  ~TraceBufferVector() {
    STLDeleteElements(&chunks_);
  }

  std::unique_ptr<TraceBufferChunk> GetChunk(size_t* index) override {
    // This function may be called when adding normal events or indirectly from
    // AddMetadataEventsWhileLocked(). We can not DCHECK(!IsFull()) because we
    // have to add the metadata events and flush thread-local buffers even if
    // the buffer is full.
    *index = chunks_.size();
    chunks_.push_back(nullptr);  // Put NULL in the slot of a in-flight chunk.
    ++in_flight_chunk_count_;
    // + 1 because zero chunk_seq is not allowed.
    return std::unique_ptr<TraceBufferChunk>(
        new TraceBufferChunk(static_cast<uint32>(*index) + 1));
  }

  virtual void ReturnChunk(size_t index,
                           std::unique_ptr<TraceBufferChunk> chunk) override {
    DCHECK_GT(in_flight_chunk_count_, 0u);
    DCHECK_LT(index, chunks_.size());
    DCHECK(!chunks_[index]);
    --in_flight_chunk_count_;
    chunks_[index] = chunk.release();
  }

  bool IsFull() const override {
    return chunks_.size() >= kTraceEventVectorBufferChunks;
  }

  size_t Size() const override {
    // This is approximate because not all of the chunks are full.
    return chunks_.size() * kTraceBufferChunkSize;
  }

  size_t Capacity() const override {
    return kTraceEventVectorBufferChunks * kTraceBufferChunkSize;
  }

  TraceEvent* GetEventByHandle(TraceEventHandle handle) override {
    if (handle.chunk_index >= chunks_.size())
      return nullptr;
    TraceBufferChunk* chunk = chunks_[handle.chunk_index];
    if (!chunk || chunk->seq() != handle.chunk_seq)
      return nullptr;
    return chunk->GetEventAt(handle.event_index);
  }

  const TraceBufferChunk* NextChunk() override {
    while (current_iteration_index_ < chunks_.size()) {
      // Skip in-flight chunks.
      const TraceBufferChunk* chunk = chunks_[current_iteration_index_++];
      if (chunk)
        return chunk;
    }
    return nullptr;
  }

  std::unique_ptr<TraceBuffer> CloneForIteration() const override {
    NOTIMPLEMENTED();
    return std::unique_ptr<TraceBuffer>();
  }

 private:
  size_t in_flight_chunk_count_;
  size_t current_iteration_index_;
  vector<TraceBufferChunk*> chunks_;

  DISALLOW_COPY_AND_ASSIGN(TraceBufferVector);
};

template <typename T>
void InitializeMetadataEvent(TraceEvent* trace_event,
                             int64_t thread_id,
                             const char* metadata_name, const char* arg_name,
                             const T& value) {
  if (!trace_event)
    return;

  int num_args = 1;
  unsigned char arg_type;
  uint64_t arg_value;
  ::trace_event_internal::SetTraceValue(value, &arg_type, &arg_value);
  trace_event->Initialize(thread_id,
                          MicrosecondsInt64(0), MicrosecondsInt64(0), TRACE_EVENT_PHASE_METADATA,
                          &g_category_group_enabled[g_category_metadata],
                          metadata_name, ::trace_event_internal::kNoEventId,
                          num_args, &arg_name, &arg_type, &arg_value, nullptr,
                          TRACE_EVENT_FLAG_NONE);
}

// RAII object which marks '*dst' with a non-zero value while in scope.
// This assumes that no other threads write to '*dst'.
class NODISCARD_CLASS MarkFlagInScope {
 public:
  explicit MarkFlagInScope(Atomic32* dst)
      : dst_(dst) {
    // We currently use Acquire_AtomicExchange here because it appears
    // to be the cheapest way of getting an "Acquire_Store" barrier. Actually
    // using Acquire_Store generates more assembly instructions and benchmarks
    // slightly slower.
    //
    // TODO: it would be even faster to avoid the memory barrier here entirely,
    // and do an asymmetric barrier, for example by having the flusher thread
    // send a signal to every registered thread, or wait until every other thread
    // has experienced at least one context switch. A number of options for this
    // are outlined in:
    // http://home.comcast.net/~pjbishop/Dave/Asymmetric-Dekker-Synchronization.txt
    Atomic32 old_val = base::subtle::Acquire_AtomicExchange(dst_, 1);
    DCHECK_EQ(old_val, 0);
  }
  ~MarkFlagInScope() {
    base::subtle::Release_Store(dst_, 0);
  }

 private:
  Atomic32* dst_;
  DISALLOW_COPY_AND_ASSIGN(MarkFlagInScope);
};
}  // anonymous namespace

TraceLog::ThreadLocalEventBuffer* TraceLog::PerThreadInfo::AtomicTakeBuffer() {
  return reinterpret_cast<TraceLog::ThreadLocalEventBuffer*>(
    base::subtle::Acquire_AtomicExchange(
      reinterpret_cast<AtomicWord*>(&event_buffer_),
      0));
}

void TraceBufferChunk::Reset(uint32 new_seq) {
  for (size_t i = 0; i < next_free_; ++i)
    chunk_[i].Reset();
  next_free_ = 0;
  seq_ = new_seq;
}

TraceEvent* TraceBufferChunk::AddTraceEvent(size_t* event_index) {
  DCHECK(!IsFull());
  *event_index = next_free_++;
  return &chunk_[*event_index];
}

std::unique_ptr<TraceBufferChunk> TraceBufferChunk::Clone() const {
  std::unique_ptr<TraceBufferChunk> cloned_chunk(new TraceBufferChunk(seq_));
  cloned_chunk->next_free_ = next_free_;
  for (size_t i = 0; i < next_free_; ++i)
    cloned_chunk->chunk_[i].CopyFrom(chunk_[i]);
  return cloned_chunk;
}

// A helper class that allows the lock to be acquired in the middle of the scope
// and unlocks at the end of scope if locked.
class TraceLog::OptionalAutoLock {
 public:
  explicit OptionalAutoLock(base::SpinLock* lock)
      : lock_(lock),
        locked_(false) {
  }

  ~OptionalAutoLock() {
    if (locked_)
      lock_->Unlock();
  }

  void EnsureAcquired() ACQUIRE() {
    if (!locked_) {
      lock_->Lock();
      locked_ = true;
    }
  }

 private:
  base::SpinLock* lock_;
  bool locked_;
  DISALLOW_COPY_AND_ASSIGN(OptionalAutoLock);
};

// Use this function instead of TraceEventHandle constructor to keep the
// overhead of ScopedTracer (trace_event.h) constructor minimum.
void MakeHandle(uint32 chunk_seq, size_t chunk_index, size_t event_index,
                TraceEventHandle* handle) {
  DCHECK(chunk_seq);
  DCHECK(chunk_index < (1u << 16));
  DCHECK(event_index < (1u << 16));
  handle->chunk_seq = chunk_seq;
  handle->chunk_index = static_cast<uint16>(chunk_index);
  handle->event_index = static_cast<uint16>(event_index);
}

////////////////////////////////////////////////////////////////////////////////
//
// TraceEvent
//
////////////////////////////////////////////////////////////////////////////////

namespace {

size_t GetAllocLength(const char* str) { return str ? strlen(str) + 1 : 0; }

// Copies |*member| into |*buffer|, sets |*member| to point to this new
// location, and then advances |*buffer| by the amount written.
void CopyTraceEventParameter(char** buffer,
                             const char** member,
                             const char* end) {
  if (*member) {
    size_t written = strings::strlcpy(*buffer, *member, end - *buffer) + 1;
    DCHECK_LE(static_cast<int>(written), end - *buffer);
    *member = *buffer;
    *buffer += written;
  }
}

}  // namespace

TraceEvent::TraceEvent()
    : duration_(-1),
      thread_duration_(-1),
      id_(0u),
      category_group_enabled_(nullptr),
      name_(nullptr),
      thread_id_(0),
      phase_(TRACE_EVENT_PHASE_BEGIN),
      flags_(0) {
  for (auto& arg_name : arg_names_) {
    arg_name = nullptr;
  }
  memset(arg_values_, 0, sizeof(arg_values_));
}

TraceEvent::~TraceEvent() {
}

void TraceEvent::CopyFrom(const TraceEvent& other) {
  timestamp_ = other.timestamp_;
  thread_timestamp_ = other.thread_timestamp_;
  duration_ = other.duration_;
  id_ = other.id_;
  category_group_enabled_ = other.category_group_enabled_;
  name_ = other.name_;
  thread_id_ = other.thread_id_;
  phase_ = other.phase_;
  flags_ = other.flags_;
  parameter_copy_storage_ = other.parameter_copy_storage_;

  for (int i = 0; i < kTraceMaxNumArgs; ++i) {
    arg_names_[i] = other.arg_names_[i];
    arg_types_[i] = other.arg_types_[i];
    arg_values_[i] = other.arg_values_[i];
    convertable_values_[i] = other.convertable_values_[i];
  }
}

void TraceEvent::Initialize(
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
    unsigned char flags) {
  timestamp_ = timestamp;
  thread_timestamp_ = thread_timestamp;
  duration_ = -1;;
  id_ = id;
  category_group_enabled_ = category_group_enabled;
  name_ = name;
  thread_id_ = thread_id;
  phase_ = phase;
  flags_ = flags;

  // Clamp num_args since it may have been set by a third_party library.
  num_args = (num_args > kTraceMaxNumArgs) ? kTraceMaxNumArgs : num_args;
  int i = 0;
  for (; i < num_args; ++i) {
    arg_names_[i] = arg_names[i];
    arg_types_[i] = arg_types[i];

    if (arg_types[i] == TRACE_VALUE_TYPE_CONVERTABLE)
      convertable_values_[i] = convertable_values[i];
    else
      arg_values_[i].as_uint = arg_values[i];
  }
  for (; i < kTraceMaxNumArgs; ++i) {
    arg_names_[i] = nullptr;
    arg_values_[i].as_uint = 0u;
    convertable_values_[i] = nullptr;
    arg_types_[i] = TRACE_VALUE_TYPE_UINT;
  }

  bool copy = !!(flags & TRACE_EVENT_FLAG_COPY);
  size_t alloc_size = 0;
  if (copy) {
    alloc_size += GetAllocLength(name);
    for (i = 0; i < num_args; ++i) {
      alloc_size += GetAllocLength(arg_names_[i]);
      if (arg_types_[i] == TRACE_VALUE_TYPE_STRING)
        arg_types_[i] = TRACE_VALUE_TYPE_COPY_STRING;
    }
  }

  bool arg_is_copy[kTraceMaxNumArgs];
  for (i = 0; i < num_args; ++i) {
    // No copying of convertible types, we retain ownership.
    if (arg_types_[i] == TRACE_VALUE_TYPE_CONVERTABLE) {
      arg_is_copy[i] = false;  // Without this, clang analyzer complaisn below.
      continue;
    }

    // We only take a copy of arg_vals if they are of type COPY_STRING.
    arg_is_copy[i] = (arg_types_[i] == TRACE_VALUE_TYPE_COPY_STRING);
    if (arg_is_copy[i])
      alloc_size += GetAllocLength(arg_values_[i].as_string);
  }

  if (alloc_size) {
    parameter_copy_storage_ = new RefCountedString;
    parameter_copy_storage_->data().resize(alloc_size);
    char* ptr = string_as_array(&parameter_copy_storage_->data());
    const char* end = ptr + alloc_size;
    if (copy) {
      CopyTraceEventParameter(&ptr, &name_, end);
      for (i = 0; i < num_args; ++i) {
        CopyTraceEventParameter(&ptr, &arg_names_[i], end);
      }
    }
    for (i = 0; i < num_args; ++i) {
      if (arg_types_[i] == TRACE_VALUE_TYPE_CONVERTABLE)
        continue;
      // Without the assignment to arg_is_copy[i] before a continue statement above, clang
      // analyzer says this here:
      // warning: Branch condition evaluates to a garbage value
      if (arg_is_copy[i]) {
        CopyTraceEventParameter(&ptr, &arg_values_[i].as_string, end);
      }
    }
    DCHECK_EQ(end, ptr) << "Overrun by " << ptr - end;
  }
}

void TraceEvent::Reset() {
  // Only reset fields that won't be initialized in Initialize(), or that may
  // hold references to other objects.
  duration_ = -1;;
  parameter_copy_storage_ = nullptr;
  for (int i = 0; i < kTraceMaxNumArgs && arg_names_[i]; ++i)
    convertable_values_[i] = nullptr;
}

void TraceEvent::UpdateDuration(const MicrosecondsInt64& now,
                                const MicrosecondsInt64& thread_now) {
  DCHECK_EQ(duration_, -1);
  duration_ = now - timestamp_;
  thread_duration_ = thread_now - thread_timestamp_;
}

// static
void TraceEvent::AppendValueAsJSON(unsigned char type,
                                   TraceEvent::TraceValue value,
                                   std::string* out) {
  switch (type) {
    case TRACE_VALUE_TYPE_BOOL:
      *out += value.as_bool ? "true" : "false";
      break;
    case TRACE_VALUE_TYPE_UINT:
      SubstituteAndAppend(out, "$0", static_cast<uint64>(value.as_uint));
      break;
    case TRACE_VALUE_TYPE_INT:
      SubstituteAndAppend(out, "$0", static_cast<int64>(value.as_int));
      break;
    case TRACE_VALUE_TYPE_DOUBLE: {
      // FIXME: base/json/json_writer.cc is using the same code,
      //        should be made into a common method.
      std::string real;
      double val = value.as_double;
      if (MathLimits<double>::IsFinite(val)) {
        real = strings::Substitute("$0", val);
        // Ensure that the number has a .0 if there's no decimal or 'e'.  This
        // makes sure that when we read the JSON back, it's interpreted as a
        // real rather than an int.
        if (real.find('.') == std::string::npos &&
            real.find('e') == std::string::npos &&
            real.find('E') == std::string::npos) {
          real.append(".0");
        }
        // The JSON spec requires that non-integer values in the range (-1,1)
        // have a zero before the decimal point - ".52" is not valid, "0.52" is.
        if (real[0] == '.') {
          real.insert(0, "0");
        } else if (real.length() > 1 && real[0] == '-' && real[1] == '.') {
          // "-.1" bad "-0.1" good
          real.insert(1, "0");
        }
      } else if (MathLimits<double>::IsNaN(val)) {
        // The JSON spec doesn't allow NaN and Infinity (since these are
        // objects in EcmaScript).  Use strings instead.
        real = "\"NaN\"";
      } else if (val < 0) {
        real = "\"-Infinity\"";
      } else {
        real = "\"Infinity\"";
      }
      SubstituteAndAppend(out, "$0", real);
      break;
    }
    case TRACE_VALUE_TYPE_POINTER:
      // JSON only supports double and int numbers.
      // So as not to lose bits from a 64-bit pointer, output as a hex string.
      StringAppendF(out, "\"0x%" PRIx64 "\"", static_cast<uint64>(
                                     reinterpret_cast<intptr_t>(
                                     value.as_pointer)));
      break;
    case TRACE_VALUE_TYPE_STRING:
    case TRACE_VALUE_TYPE_COPY_STRING:
      *out += "\"";
      JsonEscape(value.as_string ? value.as_string : "NULL", out);
      *out += "\"";
      break;
    default:
      LOG(FATAL) << "Don't know how to print this value";
      break;
  }
}

void TraceEvent::AppendAsJSON(std::string* out) const {
  int64 time_int64 = timestamp_;
  int process_id = TraceLog::GetInstance()->process_id();
  // Category group checked at category creation time.
  DCHECK(!strchr(name_, '"'));
  StringAppendF(out,
      "{\"cat\":\"%s\",\"pid\":%i,\"tid\":%" PRId64 ",\"ts\":%" PRId64 ","
      "\"ph\":\"%c\",\"name\":\"%s\",\"args\":{",
      TraceLog::GetCategoryGroupName(category_group_enabled_),
      process_id,
      thread_id_,
      time_int64,
      phase_,
      name_);

  // Output argument names and values, stop at first NULL argument name.
  for (int i = 0; i < kTraceMaxNumArgs && arg_names_[i]; ++i) {
    if (i > 0)
      *out += ",";
    *out += "\"";
    *out += arg_names_[i];
    *out += "\":";

    if (arg_types_[i] == TRACE_VALUE_TYPE_CONVERTABLE)
      convertable_values_[i]->AppendAsTraceFormat(out);
    else
      AppendValueAsJSON(arg_types_[i], arg_values_[i], out);
  }
  *out += "}";

  if (phase_ == TRACE_EVENT_PHASE_COMPLETE) {
    int64 duration = duration_;
    if (duration != -1)
      StringAppendF(out, ",\"dur\":%" PRId64, duration);
    if (thread_timestamp_ >= 0) {
      int64 thread_duration = thread_duration_;
      if (thread_duration != -1)
        StringAppendF(out, ",\"tdur\":%" PRId64, thread_duration);
    }
  }

  // Output tts if thread_timestamp is valid.
  if (thread_timestamp_ >= 0) {
    int64 thread_time_int64 = thread_timestamp_;
    StringAppendF(out, ",\"tts\":%" PRId64, thread_time_int64);
  }

  // If id_ is set, print it out as a hex string so we don't loose any
  // bits (it might be a 64-bit pointer).
  if (flags_ & TRACE_EVENT_FLAG_HAS_ID)
    StringAppendF(out, ",\"id\":\"0x%" PRIx64 "\"", static_cast<uint64>(id_));

  // Instant events also output their scope.
  if (phase_ == TRACE_EVENT_PHASE_INSTANT) {
    char scope = '?';
    switch (flags_ & TRACE_EVENT_FLAG_SCOPE_MASK) {
      case TRACE_EVENT_SCOPE_GLOBAL:
        scope = TRACE_EVENT_SCOPE_NAME_GLOBAL;
        break;

      case TRACE_EVENT_SCOPE_PROCESS:
        scope = TRACE_EVENT_SCOPE_NAME_PROCESS;
        break;

      case TRACE_EVENT_SCOPE_THREAD:
        scope = TRACE_EVENT_SCOPE_NAME_THREAD;
        break;
    }
    StringAppendF(out, ",\"s\":\"%c\"", scope);
  }

  *out += "}";
}

void TraceEvent::AppendPrettyPrinted(std::ostringstream* out) const {
  *out << name_ << "[";
  *out << TraceLog::GetCategoryGroupName(category_group_enabled_);
  *out << "]";
  if (arg_names_[0]) {
    *out << ", {";
    for (int i = 0; i < kTraceMaxNumArgs && arg_names_[i]; ++i) {
      if (i > 0)
        *out << ", ";
      *out << arg_names_[i] << ":";
      std::string value_as_text;

      if (arg_types_[i] == TRACE_VALUE_TYPE_CONVERTABLE)
        convertable_values_[i]->AppendAsTraceFormat(&value_as_text);
      else
        AppendValueAsJSON(arg_types_[i], arg_values_[i], &value_as_text);

      *out << value_as_text;
    }
    *out << "}";
  }
}

////////////////////////////////////////////////////////////////////////////////
//
// TraceResultBuffer
//
////////////////////////////////////////////////////////////////////////////////

string TraceResultBuffer::FlushTraceLogToString() {
  return DoFlush(false);
}

string TraceResultBuffer::FlushTraceLogToStringButLeaveBufferIntact() {
  return DoFlush(true);
}

string TraceResultBuffer::DoFlush(bool leave_intact) {
  TraceResultBuffer buf;
  TraceLog* tl = TraceLog::GetInstance();
  if (leave_intact) {
    tl->FlushButLeaveBufferIntact(Bind(&TraceResultBuffer::Collect, Unretained(&buf)));
  } else {
    tl->Flush(Bind(&TraceResultBuffer::Collect, Unretained(&buf)));
  }
  buf.json_.append("]}\n");
  return buf.json_;
}

TraceResultBuffer::TraceResultBuffer()
  : first_(true) {
}
TraceResultBuffer::~TraceResultBuffer() {
}

void TraceResultBuffer::Collect(
  const scoped_refptr<RefCountedString>& s,
  bool has_more_events) {
  if (first_) {
    json_.append("{\"traceEvents\": [\n");
    first_ = false;
  } else if (!s->data().empty()) {
    // Sometimes we get sent an empty chunk at the end,
    // and we don't want to end up with an extra trailing ','.
    json_.append(",\n");
  }
  json_.append(s->data());
}

////////////////////////////////////////////////////////////////////////////////
//
// TraceSamplingThread
//
////////////////////////////////////////////////////////////////////////////////
class TraceBucketData;
typedef Callback<void(TraceBucketData*)> TraceSampleCallback;

class TraceBucketData {
 public:
  TraceBucketData(AtomicWord* bucket,
                  const char* name,
                  TraceSampleCallback callback);
  ~TraceBucketData();

  TRACE_EVENT_API_ATOMIC_WORD* bucket;
  const char* bucket_name;
  TraceSampleCallback callback;
};

// This object must be created on the IO thread.
class TraceSamplingThread {
 public:
  TraceSamplingThread();
  virtual ~TraceSamplingThread();

  void ThreadMain();

  static void DefaultSamplingCallback(TraceBucketData* bucekt_data);

  void Stop();

 private:
  friend class TraceLog;

  void GetSamples();
  // Not thread-safe. Once the ThreadMain has been called, this can no longer
  // be called.
  void RegisterSampleBucket(TRACE_EVENT_API_ATOMIC_WORD* bucket,
                            const char* const name,
                            TraceSampleCallback callback);
  // Splits a combined "category\0name" into the two component parts.
  static void ExtractCategoryAndName(const char* combined,
                                     const char** category,
                                     const char** name);
  std::vector<TraceBucketData> sample_buckets_;
  bool thread_running_;
  AtomicBool cancellation_flag_;
};


TraceSamplingThread::TraceSamplingThread()
  : thread_running_(false),
    cancellation_flag_(false) {
}

TraceSamplingThread::~TraceSamplingThread() {
}

void TraceSamplingThread::ThreadMain() {
  thread_running_ = true;
  const MonoDelta sleepDelta = MonoDelta::FromMicroseconds(1000);
  while (!cancellation_flag_.Load()) {
    SleepFor(sleepDelta);
    GetSamples();
  }
}

// static
void TraceSamplingThread::DefaultSamplingCallback(
    TraceBucketData* bucket_data) {
  TRACE_EVENT_API_ATOMIC_WORD category_and_name =
      TRACE_EVENT_API_ATOMIC_LOAD(*bucket_data->bucket);
  if (!category_and_name)
    return;
  const char* const combined =
      reinterpret_cast<const char* const>(category_and_name);
  const char* category_group;
  const char* name;
  ExtractCategoryAndName(combined, &category_group, &name);

  TRACE_EVENT_API_ADD_TRACE_EVENT(TRACE_EVENT_PHASE_SAMPLE,
      TraceLog::GetCategoryGroupEnabled(category_group),
      name, 0, 0, nullptr, nullptr, nullptr, nullptr, 0);
}

void TraceSamplingThread::GetSamples() {
  for (auto& sample_bucket : sample_buckets_) {
    TraceBucketData* bucket_data = &sample_bucket;
    bucket_data->callback.Run(bucket_data);
  }
}

void TraceSamplingThread::RegisterSampleBucket(
    TRACE_EVENT_API_ATOMIC_WORD* bucket,
    const char* const name,
    TraceSampleCallback callback) {
  // Access to sample_buckets_ doesn't cause races with the sampling thread
  // that uses the sample_buckets_, because it is guaranteed that
  // RegisterSampleBucket is called before the sampling thread is created.
  DCHECK(!thread_running_);
  sample_buckets_.push_back(TraceBucketData(bucket, name, callback));
}

// static
void TraceSamplingThread::ExtractCategoryAndName(const char* combined,
                                                 const char** category,
                                                 const char** name) {
  *category = combined;
  *name = &combined[strlen(combined) + 1];
}

void TraceSamplingThread::Stop() {
  cancellation_flag_.Store(true);
}

TraceBucketData::TraceBucketData(AtomicWord* bucket, const char* name,
                                 TraceSampleCallback callback)
    : bucket(bucket), bucket_name(name), callback(std::move(callback)) {}

TraceBucketData::~TraceBucketData() {
}

////////////////////////////////////////////////////////////////////////////////
//
// TraceLog
//
////////////////////////////////////////////////////////////////////////////////

class TraceLog::ThreadLocalEventBuffer {
 public:
  explicit ThreadLocalEventBuffer(TraceLog* trace_log);
  virtual ~ThreadLocalEventBuffer();

  TraceEvent* AddTraceEvent(TraceEventHandle* handle);

  TraceEvent* GetEventByHandle(TraceEventHandle handle) {
    if (!chunk_ || handle.chunk_seq != chunk_->seq() ||
        handle.chunk_index != chunk_index_)
      return nullptr;

    return chunk_->GetEventAt(handle.event_index);
  }

  int generation() const { return generation_; }

  void Flush(int64_t tid);

 private:
  // Check that the current thread is the one that constructed this trace buffer.
  void CheckIsOwnerThread() const {
    DCHECK_EQ(yb::Thread::UniqueThreadId(), owner_tid_);
  }

  // Since TraceLog is a leaky singleton, trace_log_ will always be valid
  // as long as the thread exists.
  TraceLog* trace_log_;
  std::unique_ptr<TraceBufferChunk> chunk_;
  size_t chunk_index_;
  int generation_;

  // The TID of the thread that constructed this event buffer. Only this thread
  // may add trace events.
  int64_t owner_tid_;

  DISALLOW_COPY_AND_ASSIGN(ThreadLocalEventBuffer);
};

TraceLog::ThreadLocalEventBuffer::ThreadLocalEventBuffer(TraceLog* trace_log)
    : trace_log_(trace_log),
      chunk_index_(0),
      generation_(trace_log->generation()),
      owner_tid_(yb::Thread::UniqueThreadId()) {
}

TraceLog::ThreadLocalEventBuffer::~ThreadLocalEventBuffer() {
}

TraceEvent* TraceLog::ThreadLocalEventBuffer::AddTraceEvent(
    TraceEventHandle* handle) {
  CheckIsOwnerThread();

  if (chunk_ && chunk_->IsFull()) {
    SpinLockHolder lock(&trace_log_->lock_);
    Flush(Thread::UniqueThreadId());
    chunk_.reset();
  }
  if (!chunk_) {
    SpinLockHolder lock(&trace_log_->lock_);
    chunk_ = trace_log_->logged_events_->GetChunk(&chunk_index_);
    trace_log_->CheckIfBufferIsFullWhileLocked();
  }
  if (!chunk_)
    return nullptr;

  size_t event_index;
  TraceEvent* trace_event = chunk_->AddTraceEvent(&event_index);
  if (trace_event && handle)
    MakeHandle(chunk_->seq(), chunk_index_, event_index, handle);

  return trace_event;
}

void TraceLog::ThreadLocalEventBuffer::Flush(int64_t tid) {
  DCHECK(trace_log_->lock_.IsHeld());

  if (!chunk_)
    return;

  if (trace_log_->CheckGeneration(generation_)) {
    // Return the chunk to the buffer only if the generation matches.
    trace_log_->logged_events_->ReturnChunk(chunk_index_, std::move(chunk_));
  }
}

// static
TraceLog* TraceLog::GetInstance() {
  return Singleton<TraceLog>::get();
}

TraceLog::TraceLog()
    : mode_(DISABLED),
      num_traces_recorded_(0),
      event_callback_(0),
      dispatching_to_observer_list_(false),
      process_sort_index_(0),
      process_id_hash_(0),
      process_id_(0),
      time_offset_(0),
      watch_category_(0),
      trace_options_(RECORD_UNTIL_FULL),
      sampling_thread_handle_(nullptr),
      category_filter_(CategoryFilter::kDefaultCategoryFilterString),
      event_callback_category_filter_(
          CategoryFilter::kDefaultCategoryFilterString),
      thread_shared_chunk_index_(0),
      generation_(0) {
  // Trace is enabled or disabled on one thread while other threads are
  // accessing the enabled flag. We don't care whether edge-case events are
  // traced or not, so we allow races on the enabled flag to keep the trace
  // macros fast.
  ANNOTATE_BENIGN_RACE_SIZED(g_category_group_enabled,
                             sizeof(g_category_group_enabled),
                             "trace_event category enabled");
  for (int i = 0; i < MAX_CATEGORY_GROUPS; ++i) {
    ANNOTATE_BENIGN_RACE(&g_category_group_enabled[i],
                         "trace_event category enabled");
  }
  SetProcessID(static_cast<int>(getpid()));

  string filter = FLAGS_trace_to_console;
  if (!filter.empty()) {
    SetEnabled(CategoryFilter(filter), RECORDING_MODE, ECHO_TO_CONSOLE);
    LOG(ERROR) << "Tracing to console with CategoryFilter '" << filter << "'.";
  }

  logged_events_.reset(CreateTraceBuffer());
}

TraceLog::~TraceLog() {
}

const unsigned char* TraceLog::GetCategoryGroupEnabled(
    const char* category_group) {
  TraceLog* tracelog = GetInstance();
  if (!tracelog) {
    DCHECK(!g_category_group_enabled[g_category_already_shutdown]);
    return &g_category_group_enabled[g_category_already_shutdown];
  }
  return tracelog->GetCategoryGroupEnabledInternal(category_group);
}

const char* TraceLog::GetCategoryGroupName(
    const unsigned char* category_group_enabled) {
  // Calculate the index of the category group by finding
  // category_group_enabled in g_category_group_enabled array.
  uintptr_t category_begin =
      reinterpret_cast<uintptr_t>(g_category_group_enabled);
  uintptr_t category_ptr = reinterpret_cast<uintptr_t>(category_group_enabled);
  DCHECK(category_ptr >= category_begin &&
         category_ptr < reinterpret_cast<uintptr_t>(
             g_category_group_enabled + MAX_CATEGORY_GROUPS)) <<
      "out of bounds category pointer";
  uintptr_t category_index =
      (category_ptr - category_begin) / sizeof(g_category_group_enabled[0]);
  return g_category_groups[category_index];
}

void TraceLog::UpdateCategoryGroupEnabledFlag(AtomicWord category_index) {
  unsigned char enabled_flag = 0;
  const char* category_group = g_category_groups[category_index];
  if (mode_ == RECORDING_MODE &&
      category_filter_.IsCategoryGroupEnabled(category_group))
    enabled_flag |= ENABLED_FOR_RECORDING;
  else if (mode_ == MONITORING_MODE &&
      category_filter_.IsCategoryGroupEnabled(category_group))
    enabled_flag |= ENABLED_FOR_MONITORING;
  if (event_callback_ &&
      event_callback_category_filter_.IsCategoryGroupEnabled(category_group))
    enabled_flag |= ENABLED_FOR_EVENT_CALLBACK;
  g_category_group_enabled[category_index] = enabled_flag;
}

void TraceLog::UpdateCategoryGroupEnabledFlags() {
  auto category_index = base::subtle::NoBarrier_Load(&g_category_index);
  for (AtomicWord i = 0; i < category_index; i++)
    UpdateCategoryGroupEnabledFlag(i);
}

void TraceLog::UpdateSyntheticDelaysFromCategoryFilter() {
  ResetTraceEventSyntheticDelays();
  const CategoryFilter::StringList& delays =
      category_filter_.GetSyntheticDelayValues();
  CategoryFilter::StringList::const_iterator ci;
  for (ci = delays.begin(); ci != delays.end(); ++ci) {
    std::list<string> tokens = strings::Split(*ci, ";");
    if (tokens.empty()) continue;

    TraceEventSyntheticDelay* delay =
        TraceEventSyntheticDelay::Lookup(tokens.front());
    tokens.pop_front();
    while (!tokens.empty()) {
      std::string token = tokens.front();
      tokens.pop_front();
      char* duration_end;
      double target_duration = strtod(token.c_str(), &duration_end);
      if (duration_end != token.c_str()) {
        delay->SetTargetDuration(MonoDelta::FromSeconds(target_duration));
      } else if (token == "static") {
        delay->SetMode(TraceEventSyntheticDelay::STATIC);
      } else if (token == "oneshot") {
        delay->SetMode(TraceEventSyntheticDelay::ONE_SHOT);
      } else if (token == "alternating") {
        delay->SetMode(TraceEventSyntheticDelay::ALTERNATING);
      }
    }
  }
}

const unsigned char* TraceLog::GetCategoryGroupEnabledInternal(
    const char* category_group) {
  DCHECK(!strchr(category_group, '"')) <<
      "Category groups may not contain double quote";
  // The g_category_groups is append only, avoid using a lock for the fast path.
  auto current_category_index = base::subtle::Acquire_Load(&g_category_index);

  // Search for pre-existing category group.
  for (AtomicWord i = 0; i < current_category_index; ++i) {
    if (strcmp(g_category_groups[i], category_group) == 0) {
      return &g_category_group_enabled[i];
    }
  }

  unsigned char* category_group_enabled = nullptr;
  // This is the slow path: the lock is not held in the case above, so more
  // than one thread could have reached here trying to add the same category.
  // Only hold to lock when actually appending a new category, and
  // check the categories groups again.
  SpinLockHolder lock(&lock_);
  auto category_index = base::subtle::Acquire_Load(&g_category_index);
  for (AtomicWord i = 0; i < category_index; ++i) {
    if (strcmp(g_category_groups[i], category_group) == 0) {
      return &g_category_group_enabled[i];
    }
  }

  // Create a new category group.
  DCHECK(category_index < MAX_CATEGORY_GROUPS) <<
      "must increase MAX_CATEGORY_GROUPS";
  if (category_index < MAX_CATEGORY_GROUPS) {
    // Don't hold on to the category_group pointer, so that we can create
    // category groups with strings not known at compile time (this is
    // required by SetWatchEvent).
    const char* new_group = strdup(category_group);
    // NOTE: new_group is leaked, but this is a small finite amount of data.
    g_category_groups[category_index] = new_group;
    DCHECK(!g_category_group_enabled[category_index]);
    // Note that if both included and excluded patterns in the
    // CategoryFilter are empty, we exclude nothing,
    // thereby enabling this category group.
    UpdateCategoryGroupEnabledFlag(category_index);
    category_group_enabled = &g_category_group_enabled[category_index];
    // Update the max index now.
    base::subtle::Release_Store(&g_category_index, category_index + 1);
  } else {
    category_group_enabled =
        &g_category_group_enabled[g_category_categories_exhausted];
  }
  return category_group_enabled;
}

void TraceLog::GetKnownCategoryGroups(
    std::vector<std::string>* category_groups) {
  SpinLockHolder lock(&lock_);
  auto category_index = base::subtle::NoBarrier_Load(&g_category_index);
  for (AtomicWord i = g_num_builtin_categories; i < category_index; i++)
    category_groups->push_back(g_category_groups[i]);
}

void TraceLog::SetEnabled(const CategoryFilter& category_filter,
                          Mode mode,
                          Options options) {
  std::vector<EnabledStateObserver*> observer_list;
  {
    SpinLockHolder lock(&lock_);

    // Can't enable tracing when Flush() is in progress.
    Options old_options = trace_options();

    if (IsEnabled()) {
      if (options != old_options) {
        DLOG(ERROR) << "Attempting to re-enable tracing with a different "
                    << "set of options.";
      }

      if (mode != mode_) {
        DLOG(ERROR) << "Attempting to re-enable tracing with a different mode.";
      }

      category_filter_.Merge(category_filter);
      UpdateCategoryGroupEnabledFlags();
      return;
    }

    if (dispatching_to_observer_list_) {
      DLOG(ERROR) <<
          "Cannot manipulate TraceLog::Enabled state from an observer.";
      return;
    }

    mode_ = mode;

    if (options != old_options) {
      base::subtle::NoBarrier_Store(&trace_options_, options);
      UseNextTraceBuffer();
    }

    num_traces_recorded_++;

    category_filter_ = CategoryFilter(category_filter);
    UpdateCategoryGroupEnabledFlags();
    UpdateSyntheticDelaysFromCategoryFilter();

    if (options & ENABLE_SAMPLING) {
      sampling_thread_.reset(new TraceSamplingThread);
      sampling_thread_->RegisterSampleBucket(
          &g_trace_state[0],
          "bucket0",
          Bind(&TraceSamplingThread::DefaultSamplingCallback));
      sampling_thread_->RegisterSampleBucket(
          &g_trace_state[1],
          "bucket1",
          Bind(&TraceSamplingThread::DefaultSamplingCallback));
      sampling_thread_->RegisterSampleBucket(
          &g_trace_state[2],
          "bucket2",
          Bind(&TraceSamplingThread::DefaultSamplingCallback));

      Status s = Thread::Create("tracing", "sampler",
                                &TraceSamplingThread::ThreadMain,
                                sampling_thread_.get(),
                                &sampling_thread_handle_);
      if (!s.ok()) {
        LOG(DFATAL) << "failed to create trace sampling thread: " << s.ToString();
      }
    }

    dispatching_to_observer_list_ = true;
    observer_list = enabled_state_observer_list_;
  }
  // Notify observers outside the lock in case they trigger trace events.
  for (const auto& observer : observer_list)
    observer->OnTraceLogEnabled();

  {
    SpinLockHolder lock(&lock_);
    dispatching_to_observer_list_ = false;
  }
}

CategoryFilter TraceLog::GetCurrentCategoryFilter() {
  SpinLockHolder lock(&lock_);
  return category_filter_;
}

void TraceLog::SetDisabled() {
  SpinLockHolder lock(&lock_);
  SetDisabledWhileLocked();
}

void NO_THREAD_SAFETY_ANALYSIS TraceLog::SetDisabledWhileLocked() {
  DCHECK(lock_.IsHeld());

  if (!IsEnabled())
    return;

  if (dispatching_to_observer_list_) {
    DLOG(ERROR)
        << "Cannot manipulate TraceLog::Enabled state from an observer.";
    return;
  }

  mode_ = DISABLED;

  if (sampling_thread_.get()) {
    // Stop the sampling thread.
    sampling_thread_->Stop();
    lock_.Unlock();
    sampling_thread_handle_->Join();
    lock_.Lock();
    sampling_thread_handle_.reset();
    sampling_thread_.reset();
  }

  category_filter_.Clear();
  base::subtle::NoBarrier_Store(&watch_category_, 0);
  watch_event_name_ = "";
  UpdateCategoryGroupEnabledFlags();
  AddMetadataEventsWhileLocked();

  dispatching_to_observer_list_ = true;
  std::vector<EnabledStateObserver*> observer_list =
      enabled_state_observer_list_;

  {
    // Dispatch to observers outside the lock in case the observer triggers a
    // trace event.
    lock_.Unlock();
    for (const auto& observer : observer_list)
      observer->OnTraceLogDisabled();
    lock_.Lock();
  }
  dispatching_to_observer_list_ = false;
}

int TraceLog::GetNumTracesRecorded() {
  SpinLockHolder lock(&lock_);
  if (!IsEnabled())
    return -1;
  return num_traces_recorded_;
}

void TraceLog::AddEnabledStateObserver(EnabledStateObserver* listener) {
  enabled_state_observer_list_.push_back(listener);
}

void TraceLog::RemoveEnabledStateObserver(EnabledStateObserver* listener) {
  auto it = std::find(enabled_state_observer_list_.begin(),
                      enabled_state_observer_list_.end(), listener);
  if (it != enabled_state_observer_list_.end())
    enabled_state_observer_list_.erase(it);
}

bool TraceLog::HasEnabledStateObserver(EnabledStateObserver* listener) const {
  auto it = std::find(enabled_state_observer_list_.begin(),
                      enabled_state_observer_list_.end(), listener);
  return it != enabled_state_observer_list_.end();
}

float TraceLog::GetBufferPercentFull() const {
  SpinLockHolder lock(&lock_);
  return static_cast<float>(static_cast<double>(logged_events_->Size()) /
                            logged_events_->Capacity());
}

bool TraceLog::BufferIsFull() const {
  SpinLockHolder lock(&lock_);
  return logged_events_->IsFull();
}

TraceBuffer* TraceLog::CreateTraceBuffer() {
  Options options = trace_options();
  if (options & RECORD_CONTINUOUSLY)
    return new TraceBufferRingBuffer(kTraceEventRingBufferChunks);
  else if ((options & ENABLE_SAMPLING) && mode_ == MONITORING_MODE)
    return new TraceBufferRingBuffer(kMonitorTraceEventBufferChunks);
  else if (options & ECHO_TO_CONSOLE)
    return new TraceBufferRingBuffer(kEchoToConsoleTraceEventBufferChunks);
  return new TraceBufferVector();
}

TraceEvent* TraceLog::AddEventToThreadSharedChunkWhileLocked(
    TraceEventHandle* handle, bool check_buffer_is_full) {
  DCHECK(lock_.IsHeld());

  if (thread_shared_chunk_ && thread_shared_chunk_->IsFull()) {
    logged_events_->ReturnChunk(thread_shared_chunk_index_,
                                std::move(thread_shared_chunk_));
  }

  if (!thread_shared_chunk_) {
    thread_shared_chunk_ = logged_events_->GetChunk(
        &thread_shared_chunk_index_);
    if (check_buffer_is_full)
      CheckIfBufferIsFullWhileLocked();
  }
  if (!thread_shared_chunk_)
    return nullptr;

  size_t event_index;
  TraceEvent* trace_event = thread_shared_chunk_->AddTraceEvent(&event_index);
  if (trace_event && handle) {
    MakeHandle(thread_shared_chunk_->seq(), thread_shared_chunk_index_,
               event_index, handle);
  }
  return trace_event;
}

void TraceLog::CheckIfBufferIsFullWhileLocked() {
  DCHECK(lock_.IsHeld());
  if (logged_events_->IsFull())
    SetDisabledWhileLocked();
}

void TraceLog::SetEventCallbackEnabled(const CategoryFilter& category_filter,
                                       EventCallback cb) {
  SpinLockHolder lock(&lock_);
  base::subtle::NoBarrier_Store(&event_callback_,
                          reinterpret_cast<AtomicWord>(cb));
  event_callback_category_filter_ = category_filter;
  UpdateCategoryGroupEnabledFlags();
}

void TraceLog::SetEventCallbackDisabled() {
  SpinLockHolder lock(&lock_);
  base::subtle::NoBarrier_Store(&event_callback_, 0);
  UpdateCategoryGroupEnabledFlags();
}

// Flush() works as the following:
//
// We ensure by taking the global lock that we have exactly one Flusher thread
// (the caller of this function) and some number of "target" threads. We do
// not want to block the target threads, since they are running application code,
// so this implementation takes an approach based on asymmetric synchronization.
//
// For each active thread, we grab its PerThreadInfo object, which may contain
// a pointer to its active trace chunk. We use an AtomicExchange to swap this
// out for a null pointer. This ensures that, on the *next* TRACE call made by
// that thread, it will see a NULL buffer and create a _new_ trace buffer. That
// new buffer would be assigned the generation of the next collection and we don't
// have to worry about it in the current Flush().
//
// However, the swap doesn't ensure that the thread doesn't already have a local copy of
// the 'event_buffer_' that we are trying to flush. So, if the thread is in the
// middle of a Trace call, we have to wait until it exits. We do that by spinning
// on the 'is_in_trace_event_' member of that thread's thread-local structure.
//
// After we've swapped the buffer pointer and waited on the thread to exit any
// concurrent Trace() call, we know that no other thread can hold a pointer to
// the trace buffer, and we can safely flush it and delete it.
void TraceLog::Flush(const TraceLog::OutputCallback& cb) {
  if (IsEnabled()) {
    // Can't flush when tracing is enabled because otherwise PostTask would
    // - generate more trace events;
    // - deschedule the calling thread on some platforms causing inaccurate
    //   timing of the trace events.
    scoped_refptr<RefCountedString> empty_result = new RefCountedString;
    if (!cb.is_null())
      cb.Run(empty_result, false);
    LOG(WARNING) << "Ignored TraceLog::Flush called when tracing is enabled";
    return;
  }

  int generation = this->generation();
  {
    // Holding the active threads lock ensures that no thread will exit and
    // delete its own PerThreadInfo object.
    MutexLock l(active_threads_lock_);
    for (const ActiveThreadMap::value_type& entry : active_threads_) {
      int64_t tid = entry.first;
      PerThreadInfo* thr_info = entry.second;

      // Swap out their buffer from their thread-local data.
      // After this, any _future_ trace calls on that thread will create a new buffer
      // and not use the one we obtain here.
      ThreadLocalEventBuffer* buf = thr_info->AtomicTakeBuffer();

      // If this thread hasn't traced anything since our last
      // flush, we can skip it.
      if (!buf) {
        continue;
      }

      // The buffer may still be in use by that thread if they're in a call. Sleep until
      // they aren't, so we can flush/delete their old buffer.
      //
      // It's important that we do not hold 'lock_' here, because otherwise we can get a
      // deadlock: a thread may be in the middle of a trace event (is_in_trace_event_ ==
      // true) and waiting to take lock_, while we are holding the lock and waiting for it
      // to not be in the trace event.
      while (base::subtle::Acquire_Load(&thr_info->is_in_trace_event_)) {
        sched_yield();
      }

      {
        SpinLockHolder lock(&lock_);
        buf->Flush(tid);
      }
      delete buf;
    }
  }

  {
    SpinLockHolder lock(&lock_);

    if (thread_shared_chunk_) {
      logged_events_->ReturnChunk(thread_shared_chunk_index_,
                                  std::move(thread_shared_chunk_));
    }
  }

  FinishFlush(generation, cb);
}

void TraceLog::ConvertTraceEventsToTraceFormat(
    std::unique_ptr<TraceBuffer> logged_events,
    const TraceLog::OutputCallback& flush_output_callback) {

  if (flush_output_callback.is_null())
    return;

  // The callback need to be called at least once even if there is no events
  // to let the caller know the completion of flush.
  bool has_more_events = true;
  do {
    scoped_refptr<RefCountedString> json_events_str_ptr =
        new RefCountedString();

    for (size_t i = 0; i < kTraceEventBatchChunks; ++i) {
      const TraceBufferChunk* chunk = logged_events->NextChunk();
      if (!chunk) {
        has_more_events = false;
        break;
      }
      for (size_t j = 0; j < chunk->size(); ++j) {
        if (i > 0 || j > 0)
          json_events_str_ptr->data().append(",");
        chunk->GetEventAt(j)->AppendAsJSON(&(json_events_str_ptr->data()));
      }
    }

    flush_output_callback.Run(json_events_str_ptr, has_more_events);
  } while (has_more_events);
}

void TraceLog::FinishFlush(int generation,
                           const TraceLog::OutputCallback& flush_output_callback) {
  std::unique_ptr<TraceBuffer> previous_logged_events;

  if (!CheckGeneration(generation))
    return;

  {
    SpinLockHolder lock(&lock_);

    previous_logged_events.swap(logged_events_);
    UseNextTraceBuffer();
  }

  ConvertTraceEventsToTraceFormat(std::move(previous_logged_events), flush_output_callback);
}

void TraceLog::FlushButLeaveBufferIntact(
    const TraceLog::OutputCallback& flush_output_callback) {
  std::unique_ptr<TraceBuffer> previous_logged_events;
  {
    SpinLockHolder lock(&lock_);
    if (mode_ == DISABLED || (trace_options_ & RECORD_CONTINUOUSLY) == 0) {
      scoped_refptr<RefCountedString> empty_result = new RefCountedString;
      flush_output_callback.Run(empty_result, false);
      LOG(WARNING) << "Ignored TraceLog::FlushButLeaveBufferIntact when monitoring is not enabled";
      return;
    }

    AddMetadataEventsWhileLocked();
    if (thread_shared_chunk_) {
      // Return the chunk to the main buffer to flush the sampling data.
      logged_events_->ReturnChunk(thread_shared_chunk_index_, std::move(thread_shared_chunk_));
    }
    previous_logged_events = logged_events_->CloneForIteration();
  }

  ConvertTraceEventsToTraceFormat(std::move(previous_logged_events), flush_output_callback);
}

void TraceLog::UseNextTraceBuffer() {
  logged_events_.reset(CreateTraceBuffer());
  base::subtle::NoBarrier_AtomicIncrement(&generation_, 1);
  thread_shared_chunk_.reset();
  thread_shared_chunk_index_ = 0;
}

TraceEventHandle TraceLog::AddTraceEvent(
    char phase,
    const unsigned char* category_group_enabled,
    const char* name,
    uint64_t id,
    int num_args,
    const char** arg_names,
    const unsigned char* arg_types,
    const uint64_t* arg_values,
    const scoped_refptr<ConvertableToTraceFormat>* convertable_values,
    unsigned char flags) {
  auto thread_id = Thread::UniqueThreadId();
  MicrosecondsInt64 now = GetMonoTimeMicros();
  return AddTraceEventWithThreadIdAndTimestamp(phase, category_group_enabled,
                                               name, id, thread_id, now,
                                               num_args, arg_names,
                                               arg_types, arg_values,
                                               convertable_values, flags);
}

TraceLog::PerThreadInfo* TraceLog::SetupThreadLocalBuffer() {
  int64_t cur_tid = Thread::UniqueThreadId();

  auto thr_info = new PerThreadInfo();
  thr_info->event_buffer_ = nullptr;
  thr_info->is_in_trace_event_ = 0;
  thread_local_info_ = thr_info;

  Thread* t = Thread::current_thread();
  if (t) {
    t->CallAtExit(Bind(&TraceLog::ThreadExiting, Unretained(this)));
  }

  {
    MutexLock lock(active_threads_lock_);
    InsertOrDie(&active_threads_, cur_tid, thr_info);
  }
  return thr_info;
}

void TraceLog::ThreadExiting() {
  PerThreadInfo* thr_info = thread_local_info_;
  if (!thr_info) {
    return;
  }

  int64_t cur_tid = Thread::UniqueThreadId();

  // Flush our own buffer back to the central event buffer.
  // We do the atomic exchange because a flusher thread may
  // also be trying to flush us at the same time, and we need to avoid
  // conflict.
  ThreadLocalEventBuffer* buf = thr_info->AtomicTakeBuffer();
  if (buf) {
    SpinLockHolder lock(&lock_);
    buf->Flush(Thread::UniqueThreadId());
  }
  delete buf;

  {
    MutexLock lock(active_threads_lock_);
    active_threads_.erase(cur_tid);
  }
  delete thr_info;
}

TraceEventHandle TraceLog::AddTraceEventWithThreadIdAndTimestamp(
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
    unsigned char flags) {
  TraceEventHandle handle = { 0, 0, 0 };
  if (!*category_group_enabled)
    return handle;

  DCHECK(name);

  if (flags & TRACE_EVENT_FLAG_MANGLE_ID)
    id ^= process_id_hash_;

  MicrosecondsInt64 now = OffsetTimestamp(timestamp);
  MicrosecondsInt64 thread_now = GetThreadCpuTimeMicros();

  PerThreadInfo* thr_info = thread_local_info_;
  if (PREDICT_FALSE(!thr_info)) {
    thr_info = SetupThreadLocalBuffer();
  }

  // Avoid re-entrance of AddTraceEvent. This may happen in GPU process when
  // ECHO_TO_CONSOLE is enabled: AddTraceEvent -> LOG(ERROR) ->
  // GpuProcessLogMessageHandler -> PostPendingTask -> TRACE_EVENT ...
  if (base::subtle::NoBarrier_Load(&thr_info->is_in_trace_event_))
    return handle;

  MarkFlagInScope thread_is_in_trace_event(&thr_info->is_in_trace_event_);

  ThreadLocalEventBuffer* thread_local_event_buffer =
    reinterpret_cast<ThreadLocalEventBuffer*>(
      base::subtle::NoBarrier_Load(
        reinterpret_cast<AtomicWord*>(&thr_info->event_buffer_)));

  // If we have an event buffer, but it's a left-over from a previous trace,
  // delete it.
  if (PREDICT_FALSE(thread_local_event_buffer &&
                    !CheckGeneration(thread_local_event_buffer->generation()))) {
    // We might also race against a flusher thread, so we have to atomically
    // take the buffer.
    thread_local_event_buffer = thr_info->AtomicTakeBuffer();
    delete thread_local_event_buffer;
    thread_local_event_buffer = nullptr;
  }

  // If there is no current buffer, create one for this event.
  if (PREDICT_FALSE(!thread_local_event_buffer)) {
    thread_local_event_buffer = new ThreadLocalEventBuffer(this);

    base::subtle::NoBarrier_Store(
      reinterpret_cast<AtomicWord*>(&thr_info->event_buffer_),
      reinterpret_cast<AtomicWord>(thread_local_event_buffer));
  }

  // Check and update the current thread name only if the event is for the
  // current thread to avoid locks in most cases.
  if (thread_id == Thread::UniqueThreadId()) {
    Thread* yb_thr = Thread::current_thread();
    if (yb_thr) {
      const char* new_name = yb_thr->name().c_str();
      // Check if the thread name has been set or changed since the previous
      // call (if any), but don't bother if the new name is empty. Note this will
      // not detect a thread name change within the same char* buffer address: we
      // favor common case performance over corner case correctness.
      if (PREDICT_FALSE(new_name != g_current_thread_name &&
                        new_name && *new_name)) {
        g_current_thread_name = new_name;

        SpinLockHolder thread_info_lock(&thread_info_lock_);

        auto existing_name = thread_names_.find(thread_id);
        if (existing_name == thread_names_.end()) {
          // This is a new thread id, and a new name.
          thread_names_[thread_id] = new_name;
        } else {
          // This is a thread id that we've seen before, but potentially with a
          // new name.
          std::vector<GStringPiece> existing_names = strings::Split(existing_name->second, ",");
          bool found = std::find(existing_names.begin(),
                                 existing_names.end(),
                                 new_name) != existing_names.end();
          if (!found) {
            if (existing_names.size())
              existing_name->second.push_back(',');
            existing_name->second.append(new_name);
          }
        }
      }
    }
  }

  std::string console_message;
  if (*category_group_enabled &
      (ENABLED_FOR_RECORDING | ENABLED_FOR_MONITORING)) {
    TraceEvent* trace_event = thread_local_event_buffer->AddTraceEvent(&handle);

    if (trace_event) {
      trace_event->Initialize(thread_id, now, thread_now, phase,
                              category_group_enabled, name, id,
                              num_args, arg_names, arg_types, arg_values,
                              convertable_values, flags);

#if defined(OS_ANDROID)
      trace_event->SendToATrace();
#endif
    }

    if (trace_options() & ECHO_TO_CONSOLE) {
      console_message = EventToConsoleMessage(
          phase == TRACE_EVENT_PHASE_COMPLETE ? TRACE_EVENT_PHASE_BEGIN : phase,
          timestamp, trace_event);
    }
  }

  if (PREDICT_FALSE(console_message.size()))
    LOG(ERROR) << console_message;

  if (PREDICT_FALSE(reinterpret_cast<const unsigned char*>(
                      base::subtle::NoBarrier_Load(&watch_category_)) == category_group_enabled)) {
    bool event_name_matches;
    WatchEventCallback watch_event_callback_copy;
    {
      SpinLockHolder lock(&lock_);
      event_name_matches = watch_event_name_ == name;
      watch_event_callback_copy = watch_event_callback_;
    }
    if (event_name_matches) {
      if (!watch_event_callback_copy.is_null())
        watch_event_callback_copy.Run();
    }
  }

  if (PREDICT_FALSE(*category_group_enabled & ENABLED_FOR_EVENT_CALLBACK)) {
    EventCallback event_callback = reinterpret_cast<EventCallback>(
      base::subtle::NoBarrier_Load(&event_callback_));
    if (event_callback) {
      event_callback(now,
                     phase == TRACE_EVENT_PHASE_COMPLETE ?
                         TRACE_EVENT_PHASE_BEGIN : phase,
                     category_group_enabled, name, id,
                     num_args, arg_names, arg_types, arg_values,
                     flags);
    }
  }

  return handle;
}

// May be called when a COMPLETE event ends and the unfinished event has been
// recycled (phase == TRACE_EVENT_PHASE_END and trace_event == NULL).
std::string TraceLog::EventToConsoleMessage(unsigned char phase,
                                            const MicrosecondsInt64& timestamp,
                                            TraceEvent* trace_event) {
  SpinLockHolder thread_info_lock(&thread_info_lock_);

  // The caller should translate TRACE_EVENT_PHASE_COMPLETE to
  // TRACE_EVENT_PHASE_BEGIN or TRACE_EVENT_END.
  DCHECK(phase != TRACE_EVENT_PHASE_COMPLETE);

  MicrosecondsInt64 duration = 0;
  auto thread_id = trace_event ? trace_event->thread_id() : Thread::UniqueThreadId();
  if (phase == TRACE_EVENT_PHASE_END) {
    duration = timestamp - thread_event_start_times_[thread_id].top();
    thread_event_start_times_[thread_id].pop();
  }

  std::string thread_name = thread_names_[thread_id];
  if (thread_colors_.find(thread_name) == thread_colors_.end())
    thread_colors_[thread_name] = (thread_colors_.size() % 6) + 1;

  std::ostringstream log;
  log << StringPrintf("%s: \x1b[0;3%dm",
                            thread_name.c_str(),
                            thread_colors_[thread_name]);

  size_t depth = 0;
  if (thread_event_start_times_.find(thread_id) !=
      thread_event_start_times_.end())
    depth = thread_event_start_times_[thread_id].size();

  for (size_t i = 0; i < depth; ++i)
    log << "| ";

  if (trace_event)
    trace_event->AppendPrettyPrinted(&log);
  if (phase == TRACE_EVENT_PHASE_END)
    log << StringPrintf(" (%.3f ms)", duration / 1000.0f);

  log << "\x1b[0;m";

  if (phase == TRACE_EVENT_PHASE_BEGIN)
    thread_event_start_times_[thread_id].push(timestamp);

  return log.str();
}

void TraceLog::AddTraceEventEtw(char phase,
                                const char* name,
                                const void* id,
                                const char* extra) {
#if defined(OS_WIN)
  TraceEventETWProvider::Trace(name, phase, id, extra);
#endif
  INTERNAL_TRACE_EVENT_ADD(phase, "ETW Trace Event", name,
                           TRACE_EVENT_FLAG_COPY, "id", id, "extra", extra);
}

void TraceLog::AddTraceEventEtw(char phase,
                                const char* name,
                                const void* id,
                                const std::string& extra) {
#if defined(OS_WIN)
  TraceEventETWProvider::Trace(name, phase, id, extra);
#endif
  INTERNAL_TRACE_EVENT_ADD(phase, "ETW Trace Event", name,
                           TRACE_EVENT_FLAG_COPY, "id", id, "extra", extra);
}

void TraceLog::UpdateTraceEventDuration(
    const unsigned char* category_group_enabled,
    const char* name,
    TraceEventHandle handle) {

  PerThreadInfo* thr_info = thread_local_info_;
  if (!thr_info) {
    thr_info = SetupThreadLocalBuffer();
  }

  // Avoid re-entrance of AddTraceEvent. This may happen in GPU process when
  // ECHO_TO_CONSOLE is enabled: AddTraceEvent -> LOG(ERROR) ->
  // GpuProcessLogMessageHandler -> PostPendingTask -> TRACE_EVENT ...
  if (base::subtle::NoBarrier_Load(&thr_info->is_in_trace_event_))
    return;
  MarkFlagInScope thread_is_in_trace_event(&thr_info->is_in_trace_event_);

  MicrosecondsInt64 thread_now = GetThreadCpuTimeMicros();
  MicrosecondsInt64 now = OffsetNow();

  std::string console_message;
  if (*category_group_enabled & ENABLED_FOR_RECORDING) {
    OptionalAutoLock lock(&lock_);

    TraceEvent* trace_event = GetEventByHandleInternal(handle, &lock);
    if (trace_event) {
      DCHECK(trace_event->phase() == TRACE_EVENT_PHASE_COMPLETE);
      trace_event->UpdateDuration(now, thread_now);
#if defined(OS_ANDROID)
      trace_event->SendToATrace();
#endif
    }

    if (trace_options() & ECHO_TO_CONSOLE) {
      console_message = EventToConsoleMessage(TRACE_EVENT_PHASE_END,
                                              now, trace_event);
    }
  }

  if (console_message.size())
    LOG(ERROR) << console_message;

  if (*category_group_enabled & ENABLED_FOR_EVENT_CALLBACK) {
    EventCallback event_callback = reinterpret_cast<EventCallback>(
      base::subtle::NoBarrier_Load(&event_callback_));
    if (event_callback) {
      event_callback(now, TRACE_EVENT_PHASE_END, category_group_enabled, name,
                     trace_event_internal::kNoEventId, 0, nullptr, nullptr, nullptr,
                     TRACE_EVENT_FLAG_NONE);
    }
  }
}

void TraceLog::SetWatchEvent(const std::string& category_name,
                             const std::string& event_name,
                             const WatchEventCallback& callback) {
  const unsigned char* category = GetCategoryGroupEnabled(
      category_name.c_str());
  SpinLockHolder lock(&lock_);
  base::subtle::NoBarrier_Store(&watch_category_,
                          reinterpret_cast<AtomicWord>(category));
  watch_event_name_ = event_name;
  watch_event_callback_ = callback;
}

void TraceLog::CancelWatchEvent() {
  SpinLockHolder lock(&lock_);
  base::subtle::NoBarrier_Store(&watch_category_, 0);
  watch_event_name_ = "";
  watch_event_callback_.Reset();
}

void TraceLog::AddMetadataEventsWhileLocked() {
  DCHECK(lock_.IsHeld());

#if !defined(OS_NACL)  // NaCl shouldn't expose the process id.
  InitializeMetadataEvent(AddEventToThreadSharedChunkWhileLocked(nullptr, false),
                          0,
                          "num_cpus", "number",
                          base::NumCPUs());
#endif


  int current_thread_id = static_cast<int>(yb::Thread::UniqueThreadId());
  if (process_sort_index_ != 0) {
    InitializeMetadataEvent(AddEventToThreadSharedChunkWhileLocked(nullptr, false),
                            current_thread_id,
                            "process_sort_index", "sort_index",
                            process_sort_index_);
  }

  if (process_name_.size()) {
    InitializeMetadataEvent(AddEventToThreadSharedChunkWhileLocked(nullptr, false),
                            current_thread_id,
                            "process_name", "name",
                            process_name_);
  }

  if (process_labels_.size() > 0) {
    std::vector<std::string> labels;
    for (auto& label : process_labels_) {
      labels.push_back(label.second);
    }
    InitializeMetadataEvent(AddEventToThreadSharedChunkWhileLocked(nullptr, false),
                            current_thread_id,
                            "process_labels", "labels",
                            JoinStrings(labels, ","));
  }

  // Thread sort indices.
  for (auto& sort_index : thread_sort_indices_) {
    if (sort_index.second == 0)
      continue;
    InitializeMetadataEvent(AddEventToThreadSharedChunkWhileLocked(nullptr, false),
                            sort_index.first,
                            "thread_sort_index", "sort_index",
                            sort_index.second);
  }

  // Thread names.
  SpinLockHolder thread_info_lock(&thread_info_lock_);
  for (auto& name : thread_names_) {
    if (name.second.empty())
      continue;
    InitializeMetadataEvent(AddEventToThreadSharedChunkWhileLocked(nullptr, false),
                            name.first,
                            "thread_name", "name",
                            name.second);
  }
}


TraceEvent* TraceLog::GetEventByHandle(TraceEventHandle handle) {
  return GetEventByHandleInternal(handle, nullptr);
}

TraceEvent* NO_THREAD_SAFETY_ANALYSIS TraceLog::GetEventByHandleInternal(
    TraceEventHandle handle, OptionalAutoLock* lock) {
  TraceLog::PerThreadInfo* thr_info = TraceLog::thread_local_info_;

  if (!handle.chunk_seq)
    return nullptr;

  if (thr_info) {
    ThreadLocalEventBuffer* buf =
      reinterpret_cast<ThreadLocalEventBuffer*>(
        base::subtle::NoBarrier_Load(
          reinterpret_cast<AtomicWord*>(&thr_info->event_buffer_)));

    if (buf) {
      DCHECK_EQ(1, ANNOTATE_UNPROTECTED_READ(thr_info->is_in_trace_event_));

      TraceEvent* trace_event = buf->GetEventByHandle(handle);
      if (trace_event)
        return trace_event;
    }
  }

  // The event has been out-of-control of the thread local buffer.
  // Try to get the event from the main buffer with a lock.
  if (lock)
    lock->EnsureAcquired();

  if (thread_shared_chunk_ &&
      handle.chunk_index == thread_shared_chunk_index_) {
    return handle.chunk_seq == thread_shared_chunk_->seq() ?
        thread_shared_chunk_->GetEventAt(handle.event_index) : nullptr;
  }

  return logged_events_->GetEventByHandle(handle);
}

void TraceLog::SetProcessID(int process_id) {
  process_id_ = process_id;
  // Create a FNV hash from the process ID for XORing.
  // See http://isthe.com/chongo/tech/comp/fnv/ for algorithm details.
  uint64_t offset_basis = 14695981039346656037ull;
  uint64_t fnv_prime = 1099511628211ull;
  uint64_t pid = static_cast<uint64_t>(process_id_);
  process_id_hash_ = (offset_basis ^ pid) * fnv_prime;
}

void TraceLog::SetProcessSortIndex(int sort_index) {
  SpinLockHolder lock(&lock_);
  process_sort_index_ = sort_index;
}

void TraceLog::SetProcessName(const std::string& process_name) {
  SpinLockHolder lock(&lock_);
  process_name_ = process_name;
}

void TraceLog::UpdateProcessLabel(
    int label_id, const std::string& current_label) {
  if(!current_label.length())
    return RemoveProcessLabel(label_id);

  SpinLockHolder lock(&lock_);
  process_labels_[label_id] = current_label;
}

void TraceLog::RemoveProcessLabel(int label_id) {
  SpinLockHolder lock(&lock_);
  auto it = process_labels_.find(label_id);
  if (it == process_labels_.end())
    return;

  process_labels_.erase(it);
}

void TraceLog::SetThreadSortIndex(int64_t thread_id, int sort_index) {
  SpinLockHolder lock(&lock_);
  thread_sort_indices_[static_cast<int>(thread_id)] = sort_index;
}

void TraceLog::SetTimeOffset(MicrosecondsInt64 offset) {
  time_offset_ = offset;
}

size_t TraceLog::GetObserverCountForTest() const {
  return enabled_state_observer_list_.size();
}

bool CategoryFilter::IsEmptyOrContainsLeadingOrTrailingWhitespace(
    const std::string& str) {
  return  str.empty() ||
          str.at(0) == ' ' ||
          str.at(str.length() - 1) == ' ';
}

bool CategoryFilter::DoesCategoryGroupContainCategory(
    const char* category_group,
    const char* category) const {
  DCHECK(category);
  vector<string> pieces = strings::Split(category_group, ",");
  for (const string& category_group_token : pieces) {
    // Don't allow empty tokens, nor tokens with leading or trailing space.
    DCHECK(!CategoryFilter::IsEmptyOrContainsLeadingOrTrailingWhitespace(
        category_group_token))
        << "Disallowed category string";

    if (MatchPattern(category_group_token.c_str(), category))
      return true;
  }
  return false;
}

CategoryFilter::CategoryFilter(const std::string& filter_string) {
  if (!filter_string.empty())
    Initialize(filter_string);
  else
    Initialize(CategoryFilter::kDefaultCategoryFilterString);
}

CategoryFilter::CategoryFilter(const CategoryFilter& cf)
    : included_(cf.included_),
      disabled_(cf.disabled_),
      excluded_(cf.excluded_),
      delays_(cf.delays_) {
}

CategoryFilter::~CategoryFilter() {
}

CategoryFilter& CategoryFilter::operator=(const CategoryFilter& rhs) {
  if (this == &rhs)
    return *this;

  included_ = rhs.included_;
  disabled_ = rhs.disabled_;
  excluded_ = rhs.excluded_;
  delays_ = rhs.delays_;
  return *this;
}

void CategoryFilter::Initialize(const std::string& filter_string) {
  // Tokenize list of categories, delimited by ','.
  vector<string> tokens = strings::Split(filter_string, ",");
  // Add each token to the appropriate list (included_,excluded_).
  for (string category : tokens) {
    // Ignore empty categories.
    if (category.empty())
      continue;
    // Synthetic delays are of the form 'DELAY(delay;option;option;...)'.
    if (category.find(kSyntheticDelayCategoryFilterPrefix) == 0 &&
        category.at(category.size() - 1) == ')') {
      category = category.substr(
          strlen(kSyntheticDelayCategoryFilterPrefix),
          category.size() - strlen(kSyntheticDelayCategoryFilterPrefix) - 1);
      size_t name_length = category.find(';');
      if (name_length != std::string::npos && name_length > 0 &&
          name_length != category.size() - 1) {
        delays_.push_back(category);
      }
    } else if (category.at(0) == '-') {
      // Excluded categories start with '-'.
      // Remove '-' from category string.
      category = category.substr(1);
      excluded_.push_back(category);
    } else if (category.compare(0, strlen(TRACE_DISABLED_BY_DEFAULT("")),
                                TRACE_DISABLED_BY_DEFAULT("")) == 0) {
      disabled_.push_back(category);
    } else {
      included_.push_back(category);
    }
  }
}

void CategoryFilter::WriteString(const StringList& values,
                                 std::string* out,
                                 bool included) const {
  bool prepend_comma = !out->empty();
  int token_cnt = 0;
  for (const auto& value : values) {
    if (token_cnt > 0 || prepend_comma)
      StringAppendF(out, ",");
    StringAppendF(out, "%s%s", (included ? "" : "-"), value.c_str());
    ++token_cnt;
  }
}

void CategoryFilter::WriteString(const StringList& delays,
                                 std::string* out) const {
  bool prepend_comma = !out->empty();
  int token_cnt = 0;
  for (const auto& delay : delays) {
    if (token_cnt > 0 || prepend_comma)
      StringAppendF(out, ",");
    StringAppendF(out, "%s%s)", kSyntheticDelayCategoryFilterPrefix,
                  delay.c_str());
    ++token_cnt;
  }
}

std::string CategoryFilter::ToString() const {
  std::string filter_string;
  WriteString(included_, &filter_string, true);
  WriteString(disabled_, &filter_string, true);
  WriteString(excluded_, &filter_string, false);
  WriteString(delays_, &filter_string);
  return filter_string;
}

bool CategoryFilter::IsCategoryGroupEnabled(
    const char* category_group_name) const {
  // TraceLog should call this method only as  part of enabling/disabling
  // categories.
  StringList::const_iterator ci;

  // Check the disabled- filters and the disabled-* wildcard first so that a
  // "*" filter does not include the disabled.
  for (ci = disabled_.begin(); ci != disabled_.end(); ++ci) {
    if (DoesCategoryGroupContainCategory(category_group_name, ci->c_str()))
      return true;
  }
  if (DoesCategoryGroupContainCategory(category_group_name,
                                       TRACE_DISABLED_BY_DEFAULT("*")))
    return false;

  for (ci = included_.begin(); ci != included_.end(); ++ci) {
    if (DoesCategoryGroupContainCategory(category_group_name, ci->c_str()))
      return true;
  }

  for (ci = excluded_.begin(); ci != excluded_.end(); ++ci) {
    if (DoesCategoryGroupContainCategory(category_group_name, ci->c_str()))
      return false;
  }
  // If the category group is not excluded, and there are no included patterns
  // we consider this pattern enabled.
  return included_.empty();
}

bool CategoryFilter::HasIncludedPatterns() const {
  return !included_.empty();
}

void CategoryFilter::Merge(const CategoryFilter& nested_filter) {
  // Keep included patterns only if both filters have an included entry.
  // Otherwise, one of the filter was specifying "*" and we want to honour the
  // broadest filter.
  if (HasIncludedPatterns() && nested_filter.HasIncludedPatterns()) {
    included_.insert(included_.end(),
                     nested_filter.included_.begin(),
                     nested_filter.included_.end());
  } else {
    included_.clear();
  }

  disabled_.insert(disabled_.end(),
                   nested_filter.disabled_.begin(),
                   nested_filter.disabled_.end());
  excluded_.insert(excluded_.end(),
                   nested_filter.excluded_.begin(),
                   nested_filter.excluded_.end());
  delays_.insert(delays_.end(),
                 nested_filter.delays_.begin(),
                 nested_filter.delays_.end());
}

void CategoryFilter::Clear() {
  included_.clear();
  disabled_.clear();
  excluded_.clear();
}

const CategoryFilter::StringList&
    CategoryFilter::GetSyntheticDelayValues() const {
  return delays_;
}

void EnableTraceEvents() {
  trace_events_enabled.store(true);
}

}  // namespace debug
}  // namespace yb

namespace trace_event_internal {

ScopedTraceBinaryEfficient::ScopedTraceBinaryEfficient(
    const char* category_group, const char* name) {
  // The single atom works because for now the category_group can only be "gpu".
  DCHECK_EQ(strcmp(category_group, "gpu"), 0);
  static TRACE_EVENT_API_ATOMIC_WORD atomic = 0;
  INTERNAL_TRACE_EVENT_GET_CATEGORY_INFO_CUSTOM_VARIABLES(
      category_group, atomic, category_group_enabled_);
  name_ = name;
  if (*category_group_enabled_) {
    event_handle_ =
        TRACE_EVENT_API_ADD_TRACE_EVENT_WITH_THREAD_ID_AND_TIMESTAMP(
            TRACE_EVENT_PHASE_COMPLETE, category_group_enabled_, name,
            trace_event_internal::kNoEventId,
            static_cast<int>(yb::Thread::UniqueThreadId()),
            GetMonoTimeMicros(),
            0, nullptr, nullptr, nullptr, nullptr, TRACE_EVENT_FLAG_NONE);
  }
}

ScopedTraceBinaryEfficient::~ScopedTraceBinaryEfficient() {
  if (*category_group_enabled_) {
    TRACE_EVENT_API_UPDATE_TRACE_EVENT_DURATION(category_group_enabled_,
                                                name_, event_handle_);
  }
}

int TraceEventThreadId() {
  return static_cast<int>(yb::Thread::UniqueThreadId());
}

}  // namespace trace_event_internal
