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

#include <string>

#include <gtest/gtest.h>
#include <rapidjson/document.h>
#include <rapidjson/rapidjson.h>  // NOLINT

#include "yb/gutil/casts.h"

#include "yb/util/debug/trace_event.h"
#include "yb/util/debug/trace_event_synthetic_delay.h"
#include "yb/util/debug/trace_logging.h"
#include "yb/util/status_log.h"
#include "yb/util/stopwatch.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/thread.h"
#include "yb/util/trace.h"

// Need to add rapidjson.h to the list of recognized third-party libraries in our linter.

DECLARE_uint32(trace_max_dump_size);

using yb::debug::TraceLog;
using yb::debug::TraceResultBuffer;
using yb::debug::CategoryFilter;
using rapidjson::Document;
using rapidjson::Value;
using std::string;
using std::vector;

namespace yb {

class TraceTest : public YBTest {
};

// Replace all digits in 's' with the character 'X'.
static string XOutDigits(const string& s) {
  string ret;
  ret.reserve(s.size());
  for (char c : s) {
    if (isdigit(c)) {
      ret.push_back('X');
    } else {
      ret.push_back(c);
    }
  }
  return ret;
}

TEST_F(TraceTest, TestBasic) {
  scoped_refptr<Trace> t(new Trace);
  TRACE_TO(t, "hello $0, $1", "world", 12345);
  TRACE_TO(t, "goodbye $0, $1", "cruel world", 54321);

  string result = XOutDigits(t->DumpToString(false));
  ASSERT_EQ("XXXX XX:XX:XX.XXXXXX trace-test.cc:XX] hello world, XXXXX\n"
            "XXXX XX:XX:XX.XXXXXX trace-test.cc:XX] goodbye cruel world, XXXXX\n",
            result);
}

TEST_F(TraceTest, TestDumpLargeTrace) {
  scoped_refptr<Trace> t(new Trace);
  constexpr int kNumLines = 100;
  const string kLongLine(1000, 'x');
  for (int i = 1; i <= kNumLines; i++) {
    TRACE_TO(t, "Line $0 : $1", i, kLongLine);
  }
  size_t kTraceDumpSize = t->DumpToString(true).size();
  const size_t kGlogMessageSizeLimit = google::LogMessage::kMaxLogMessageLen;

  class LogSink : public google::LogSink {
   public:
    void send(
        google::LogSeverity severity, const char* full_filename, const char* base_filename,
        int line, const struct ::tm* tm_time, const char* message, size_t message_len) {
      logged_bytes_ += message_len;
    }

    size_t logged_bytes() const { return logged_bytes_; }

   private:
    std::atomic<size_t> logged_bytes_{0};
  } log_sink;

  google::AddLogSink(&log_sink);
  size_t size_before_logging;
  size_t size_after_logging;

  LOG(INFO) << "Dumping DumpToString may result in the trace getting truncated after ~30 lines";
  size_before_logging = log_sink.logged_bytes();
  LOG(INFO) << t->DumpToString(true);
  size_after_logging = log_sink.logged_bytes();
  ASSERT_LE(size_after_logging - size_before_logging, kGlogMessageSizeLimit);
  ASSERT_LT(size_after_logging - size_before_logging, kTraceDumpSize);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_trace_max_dump_size) = std::numeric_limits<uint32>::max();
  LOG(INFO) << "DumpToLogInfo should not be truncated";
  size_before_logging = log_sink.logged_bytes();
  t->DumpToLogInfo(true);
  size_after_logging = log_sink.logged_bytes();
  ASSERT_GT(size_after_logging - size_before_logging, kGlogMessageSizeLimit);
  // We expect the output to be split into 4 lines. 4 newlines will be removed while printing.
  ASSERT_EQ(size_after_logging - size_before_logging, kTraceDumpSize - 4);

  LOG(INFO) << "with trace_max_dump_size=30000 DumpToLogInfo should be truncated";
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_trace_max_dump_size) = 30000;
  size_before_logging = log_sink.logged_bytes();
  t->DumpToLogInfo(true);
  size_after_logging = log_sink.logged_bytes();
  ASSERT_LE(size_after_logging - size_before_logging, kGlogMessageSizeLimit);
  ASSERT_LT(size_after_logging - size_before_logging, kTraceDumpSize);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_trace_max_dump_size) = 0;
  LOG(INFO) << "with trace_max_dump_size=0 DumpToLogInfo should not be truncated";
  size_before_logging = log_sink.logged_bytes();
  t->DumpToLogInfo(true);
  size_after_logging = log_sink.logged_bytes();
  ASSERT_GT(size_after_logging - size_before_logging, kGlogMessageSizeLimit);
  // We expect the output to be split into 4 lines. 4 newlines will be removed while printing.
  ASSERT_EQ(size_after_logging - size_before_logging, kTraceDumpSize - 4);

  google::RemoveLogSink(&log_sink);
}

TEST_F(TraceTest, TestAttach) {
  scoped_refptr<Trace> traceA(new Trace);
  scoped_refptr<Trace> traceB(new Trace);
  {
    ADOPT_TRACE(traceA.get());
    EXPECT_EQ(traceA.get(), Trace::CurrentTrace());
    {
      ADOPT_TRACE(traceB.get());
      EXPECT_EQ(traceB.get(), Trace::CurrentTrace());
      TRACE("hello from traceB");
    }
    EXPECT_EQ(traceA.get(), Trace::CurrentTrace());
    TRACE("hello from traceA");
  }
  EXPECT_TRUE(Trace::CurrentTrace() == nullptr);
  TRACE("this goes nowhere");

  EXPECT_EQ(XOutDigits(traceA->DumpToString(false)),
            "XXXX XX:XX:XX.XXXXXX trace-test.cc:XXX] hello from traceA\n");
  EXPECT_EQ(XOutDigits(traceB->DumpToString(false)),
            "XXXX XX:XX:XX.XXXXXX trace-test.cc:XXX] hello from traceB\n");
}

TEST_F(TraceTest, TestChildTrace) {
  scoped_refptr<Trace> traceA(new Trace);
  scoped_refptr<Trace> traceB(new Trace);
  ADOPT_TRACE(traceA.get());
  traceA->AddChildTrace(traceB.get());
  TRACE("hello from traceA");
  {
    ADOPT_TRACE(traceB.get());
    TRACE("hello from traceB");
  }
  EXPECT_EQ("XXXX XX:XX:XX.XXXXXX trace-test.cc:XXX] hello from traceA\n"
            "..  Related trace:\n"
            "..  XXXX XX:XX:XX.XXXXXX trace-test.cc:XXX] hello from traceB\n",
            XOutDigits(traceA->DumpToString(false)));
}

static void GenerateTraceEvents(int thread_id,
                                int num_events) {
  for (int i = 0; i < num_events; i++) {
    TRACE_EVENT1("test", "foo", "thread_id", thread_id);
  }
}

// Parse the dumped trace data and return the number of events
// found within, including only those with the "test" category.
int ParseAndReturnEventCount(const string& trace_json) {
  Document d;
  d.Parse<0>(trace_json.c_str());
  CHECK(d.IsObject()) << "bad json: " << trace_json;
  const Value& events_json = d["traceEvents"];
  CHECK(events_json.IsArray()) << "bad json: " << trace_json;

  // Count how many of our events were seen. We have to filter out
  // the metadata events.
  int seen_real_events = 0;
  for (rapidjson::SizeType i = 0; i < events_json.Size(); i++) {
    if (events_json[i]["cat"].GetString() == string("test")) {
      seen_real_events++;
    }
  }

  return seen_real_events;
}

TEST_F(TraceTest, TestChromeTracing) {
  const int kNumThreads = 4;
  const int kEventsPerThread = AllowSlowTests() ? 1000000 : 10000;

  TraceLog* tl = TraceLog::GetInstance();
  tl->SetEnabled(CategoryFilter(CategoryFilter::kDefaultCategoryFilterString),
                 TraceLog::RECORDING_MODE,
                 TraceLog::RECORD_CONTINUOUSLY);

  vector<scoped_refptr<Thread> > threads(kNumThreads);

  Stopwatch s;
  s.start();
  for (int i = 0; i < kNumThreads; i++) {
    CHECK_OK(Thread::Create("test", "gen-traces", &GenerateTraceEvents, i, kEventsPerThread,
                            &threads[i]));
  }

  for (int i = 0; i < kNumThreads; i++) {
    threads[i]->Join();
  }
  tl->SetDisabled();

  int total_events = kNumThreads * kEventsPerThread;
  double elapsed = s.elapsed().wall_seconds();

  LOG(INFO) << "Trace performance: " << static_cast<int>(total_events / elapsed) << " traces/sec";

  string trace_json = TraceResultBuffer::FlushTraceLogToString();

  // Verify that the JSON contains events. It won't have exactly
  // kEventsPerThread * kNumThreads because the trace buffer isn't large enough
  // for that.
  ASSERT_GE(ParseAndReturnEventCount(trace_json), 100);
}

// Test that, if a thread exits before filling a full trace buffer, we still
// see its results. This is a regression test for a bug in the earlier integration
// of Chromium tracing into YB.
TEST_F(TraceTest, TestTraceFromExitedThread) {
  TraceLog* tl = TraceLog::GetInstance();
  tl->SetEnabled(CategoryFilter(CategoryFilter::kDefaultCategoryFilterString),
                 TraceLog::RECORDING_MODE,
                 TraceLog::RECORD_CONTINUOUSLY);

  // Generate 10 trace events in a separate thread.
  int kNumEvents = 10;
  scoped_refptr<Thread> t;
  CHECK_OK(Thread::Create("test", "gen-traces", &GenerateTraceEvents, 1, kNumEvents,
                          &t));
  t->Join();
  tl->SetDisabled();
  string trace_json = TraceResultBuffer::FlushTraceLogToString();
  LOG(INFO) << trace_json;

  // Verify that the buffer contains 10 trace events
  ASSERT_EQ(10, ParseAndReturnEventCount(trace_json));
}

static void GenerateWideSpan() {
  TRACE_EVENT0("test", "GenerateWideSpan");
  for (int i = 0; i < 1000; i++) {
    TRACE_EVENT0("test", "InnerLoop");
  }
}

// Test creating a trace event which contains many other trace events.
// This ensures that we can go back and update a TraceEvent which fell in
// a different trace chunk.
TEST_F(TraceTest, TestWideSpan) {
  TraceLog* tl = TraceLog::GetInstance();
  tl->SetEnabled(CategoryFilter(CategoryFilter::kDefaultCategoryFilterString),
                 TraceLog::RECORDING_MODE,
                 TraceLog::RECORD_CONTINUOUSLY);

  scoped_refptr<Thread> t;
  CHECK_OK(Thread::Create("test", "gen-traces", &GenerateWideSpan, &t));
  t->Join();
  tl->SetDisabled();

  string trace_json = TraceResultBuffer::FlushTraceLogToString();
  ASSERT_EQ(1001, ParseAndReturnEventCount(trace_json));
}

// Regression test for KUDU-753: faulty JSON escaping when dealing with
// single quote characters.
TEST_F(TraceTest, TestJsonEncodingString) {
  TraceLog* tl = TraceLog::GetInstance();
  tl->SetEnabled(CategoryFilter(CategoryFilter::kDefaultCategoryFilterString),
                 TraceLog::RECORDING_MODE,
                 TraceLog::RECORD_CONTINUOUSLY);
  {
    TRACE_EVENT1("test", "test", "arg", "this is a test with \"'\"' and characters\nand new lines");
  }
  tl->SetDisabled();
  string trace_json = TraceResultBuffer::FlushTraceLogToString();
  ASSERT_EQ(1, ParseAndReturnEventCount(trace_json));
}

// Generate trace events continuously until 'latch' fires.
// Increment *num_events_generated for each event generated.
void GenerateTracesUntilLatch(AtomicInt<int64_t>* num_events_generated,
                              CountDownLatch* latch) {
  while (latch->count()) {
    {
      // This goes in its own scope so that the event is fully generated (with
      // both its START and END times) before we do the counter increment below.
      TRACE_EVENT0("test", "GenerateTracesUntilLatch");
    }
    num_events_generated->Increment();
  }
}

// Test starting and stopping tracing while a thread is running.
// This is a regression test for bugs in earlier versions of the imported
// trace code.
TEST_F(TraceTest, TestStartAndStopCollection) {
  TraceLog* tl = TraceLog::GetInstance();

  CountDownLatch latch(1);
  AtomicInt<int64_t> num_events_generated(0);
  scoped_refptr<Thread> t;
  CHECK_OK(Thread::Create("test", "gen-traces", &GenerateTracesUntilLatch,
                          &num_events_generated, &latch, &t));

  const int num_flushes = AllowSlowTests() ? 50 : 3;
  for (int i = 0; i < num_flushes; i++) {
    tl->SetEnabled(CategoryFilter(CategoryFilter::kDefaultCategoryFilterString),
                   TraceLog::RECORDING_MODE,
                   TraceLog::RECORD_CONTINUOUSLY);

    const int64_t num_events_before = num_events_generated.Load();
    SleepFor(MonoDelta::FromMilliseconds(10));
    const int64_t num_events_after = num_events_generated.Load();
    tl->SetDisabled();

    string trace_json = TraceResultBuffer::FlushTraceLogToString();
    // We might under-count the number of events, since we only measure the sleep,
    // and tracing is enabled before and disabled after we start counting.
    // We might also over-count by at most 1, because we could enable tracing
    // right in between creating a trace event and incrementing the counter.
    // But, we should never over-count by more than 1.
    auto expected_events_lowerbound = num_events_after - num_events_before - 1;
    int captured_events = ParseAndReturnEventCount(trace_json);
    ASSERT_GE(captured_events, expected_events_lowerbound);
  }

  latch.CountDown();
  t->Join();
}

TEST_F(TraceTest, TestChromeSampling) {
  TraceLog* tl = TraceLog::GetInstance();
  tl->SetEnabled(CategoryFilter(CategoryFilter::kDefaultCategoryFilterString),
                 TraceLog::RECORDING_MODE,
                 static_cast<TraceLog::Options>(TraceLog::RECORD_CONTINUOUSLY |
                                                TraceLog::ENABLE_SAMPLING));

  for (int i = 0; i < 100; i++) {
    switch (i % 3) {
      case 0:
        TRACE_EVENT_SET_SAMPLING_STATE("test", "state-0");
        break;
      case 1:
        TRACE_EVENT_SET_SAMPLING_STATE("test", "state-1");
        break;
      case 2:
        TRACE_EVENT_SET_SAMPLING_STATE("test", "state-2");
        break;
    }
    SleepFor(MonoDelta::FromMilliseconds(1));
  }
  tl->SetDisabled();
  string trace_json = TraceResultBuffer::FlushTraceLogToString();
  ASSERT_GT(ParseAndReturnEventCount(trace_json), 0);
}

class TraceEventCallbackTest : public YBTest {
 public:
  void SetUp() override {
    YBTest::SetUp();
    ASSERT_EQ(nullptr, s_instance);
    s_instance = this;
  }
  void TearDown() override {
    TraceLog::GetInstance()->SetDisabled();

    // Flush the buffer so that one test doesn't end up leaving any
    // extra results for the next test.
    TraceResultBuffer::FlushTraceLogToString();

    ASSERT_TRUE(!!s_instance);
    s_instance = nullptr;
    YBTest::TearDown();

  }

 protected:
  void EndTraceAndFlush() {
    TraceLog::GetInstance()->SetDisabled();
    string trace_json = TraceResultBuffer::FlushTraceLogToString();
    trace_doc_.Parse<0>(trace_json.c_str());
    LOG(INFO) << trace_json;
    ASSERT_TRUE(trace_doc_.IsObject());
    trace_parsed_ = trace_doc_["traceEvents"];
    ASSERT_TRUE(trace_parsed_.IsArray());
  }

  void DropTracedMetadataRecords() {
    // NB: rapidjson has move-semantics, like auto_ptr.
    Value old_trace_parsed;
    old_trace_parsed = trace_parsed_;
    trace_parsed_.SetArray();
    auto old_trace_parsed_size = old_trace_parsed.Size();

    for (rapidjson::SizeType i = 0; i < old_trace_parsed_size; i++) {
      Value value;
      value = old_trace_parsed[i];
      if (value.GetType() != rapidjson::kObjectType) {
        trace_parsed_.PushBack(value, trace_doc_.GetAllocator());
        continue;
      }
      string tmp;
      if (value.HasMember("ph") && strcmp(value["ph"].GetString(), "M") == 0) {
        continue;
      }

      trace_parsed_.PushBack(value, trace_doc_.GetAllocator());
    }
  }

  // Search through the given array for any dictionary which has a key
  // or value which has 'string_to_match' as a substring.
  // Returns the matching dictionary, or NULL.
  static const Value* FindTraceEntry(
    const Value& trace_parsed,
    const char* string_to_match) {
    // Scan all items
    auto trace_parsed_count = trace_parsed.Size();
    for (rapidjson::SizeType i = 0; i < trace_parsed_count; i++) {
      const Value& value = trace_parsed[i];
      if (value.GetType() != rapidjson::kObjectType) {
        continue;
      }

      for (Value::ConstMemberIterator it = value.MemberBegin();
           it != value.MemberEnd();
           ++it) {
        if (it->name.IsString() && strstr(it->name.GetString(), string_to_match) != nullptr) {
          return &value;
        }
        if (it->value.IsString() && strstr(it->value.GetString(), string_to_match) != nullptr) {
          return &value;
        }
      }
    }
    return nullptr;
  }

  // For TraceEventCallbackAndRecordingX tests.
  void VerifyCallbackAndRecordedEvents(size_t expected_callback_count,
                                       size_t expected_recorded_count) {
    // Callback events.
    EXPECT_EQ(expected_callback_count, collected_events_names_.size());
    for (size_t i = 0; i < collected_events_names_.size(); ++i) {
      EXPECT_EQ("callback", collected_events_categories_[i]);
      EXPECT_EQ("yes", collected_events_names_[i]);
    }

    // Recorded events.
    EXPECT_EQ(expected_recorded_count, trace_parsed_.Size());
    EXPECT_TRUE(FindTraceEntry(trace_parsed_, "recording"));
    EXPECT_FALSE(FindTraceEntry(trace_parsed_, "callback"));
    EXPECT_TRUE(FindTraceEntry(trace_parsed_, "yes"));
    EXPECT_FALSE(FindTraceEntry(trace_parsed_, "no"));
  }

  void VerifyCollectedEvent(size_t i,
                            unsigned phase,
                            const string& category,
                            const string& name) {
    EXPECT_EQ(phase, collected_events_phases_[i]);
    EXPECT_EQ(category, collected_events_categories_[i]);
    EXPECT_EQ(name, collected_events_names_[i]);
  }

  Document trace_doc_;
  Value trace_parsed_;

  vector<string> collected_events_categories_;
  vector<string> collected_events_names_;
  vector<unsigned char> collected_events_phases_;
  vector<MicrosecondsInt64> collected_events_timestamps_;

  static TraceEventCallbackTest* s_instance;
  static void Callback(MicrosecondsInt64 timestamp,
                       char phase,
                       const unsigned char* category_group_enabled,
                       const char* name,
                       uint64_t id,
                       int num_args,
                       const char* const arg_names[],
                       const unsigned char arg_types[],
                       const uint64_t arg_values[],
                       unsigned char flags) {
    s_instance->collected_events_phases_.push_back(phase);
    s_instance->collected_events_categories_.push_back(
        TraceLog::GetCategoryGroupName(category_group_enabled));
    s_instance->collected_events_names_.push_back(name);
    s_instance->collected_events_timestamps_.push_back(timestamp);
  }
};

TraceEventCallbackTest* TraceEventCallbackTest::s_instance;

TEST_F(TraceEventCallbackTest, TraceEventCallback) {
  TRACE_EVENT_INSTANT0("all", "before enable", TRACE_EVENT_SCOPE_THREAD);
  TraceLog::GetInstance()->SetEventCallbackEnabled(
      CategoryFilter("*"), Callback);
  TRACE_EVENT_INSTANT0("all", "event1", TRACE_EVENT_SCOPE_GLOBAL);
  TRACE_EVENT_INSTANT0("all", "event2", TRACE_EVENT_SCOPE_GLOBAL);
  {
    TRACE_EVENT0("all", "duration");
    TRACE_EVENT_INSTANT0("all", "event3", TRACE_EVENT_SCOPE_GLOBAL);
  }
  TraceLog::GetInstance()->SetEventCallbackDisabled();
  TRACE_EVENT_INSTANT0("all", "after callback removed",
                       TRACE_EVENT_SCOPE_GLOBAL);
  const auto n = std::min(collected_events_names_.size(), collected_events_phases_.size());
  for (size_t i = 0; i < n; ++i) {
    const auto& name = collected_events_names_[i];
    const auto phase = collected_events_phases_[i];
    LOG(INFO) << "Collected event #" << i << ": name=" << name << ", phase=" << phase;
  }

  ASSERT_EQ(5u, collected_events_names_.size());

  EXPECT_EQ("event1", collected_events_names_[0]);
  EXPECT_EQ(TRACE_EVENT_PHASE_INSTANT, collected_events_phases_[0]);

  EXPECT_EQ("event2", collected_events_names_[1]);
  EXPECT_EQ(TRACE_EVENT_PHASE_INSTANT, collected_events_phases_[1]);

  EXPECT_EQ("duration", collected_events_names_[2]);
  EXPECT_EQ(TRACE_EVENT_PHASE_BEGIN, collected_events_phases_[2]);

  EXPECT_EQ("event3", collected_events_names_[3]);
  EXPECT_EQ(TRACE_EVENT_PHASE_INSTANT, collected_events_phases_[3]);

  EXPECT_EQ("duration", collected_events_names_[4]);
  EXPECT_EQ(TRACE_EVENT_PHASE_END, collected_events_phases_[4]);

  for (size_t i = 1; i < collected_events_timestamps_.size(); i++) {
    EXPECT_LE(collected_events_timestamps_[i - 1],
              collected_events_timestamps_[i]);
  }
}

TEST_F(TraceEventCallbackTest, TraceEventCallbackWhileFull) {
  TraceLog::GetInstance()->SetEnabled(
      CategoryFilter("*"),
      TraceLog::RECORDING_MODE,
      TraceLog::RECORD_UNTIL_FULL);
  do {
    TRACE_EVENT_INSTANT0("all", "badger badger", TRACE_EVENT_SCOPE_GLOBAL);
  } while (!TraceLog::GetInstance()->BufferIsFull());
  TraceLog::GetInstance()->SetEventCallbackEnabled(CategoryFilter("*"),
                                                   Callback);
  TRACE_EVENT_INSTANT0("all", "a snake", TRACE_EVENT_SCOPE_GLOBAL);
  TraceLog::GetInstance()->SetEventCallbackDisabled();
  ASSERT_EQ(1u, collected_events_names_.size());
  EXPECT_EQ("a snake", collected_events_names_[0]);
}

// 1: Enable callback, enable recording, disable callback, disable recording.
TEST_F(TraceEventCallbackTest, TraceEventCallbackAndRecording1) {
  TRACE_EVENT_INSTANT0("recording", "no", TRACE_EVENT_SCOPE_GLOBAL);
  TRACE_EVENT_INSTANT0("callback", "no", TRACE_EVENT_SCOPE_GLOBAL);
  TraceLog::GetInstance()->SetEventCallbackEnabled(CategoryFilter("callback"),
                                                   Callback);
  TRACE_EVENT_INSTANT0("recording", "no", TRACE_EVENT_SCOPE_GLOBAL);
  TRACE_EVENT_INSTANT0("callback", "yes", TRACE_EVENT_SCOPE_GLOBAL);
  TraceLog::GetInstance()->SetEnabled(
      CategoryFilter("recording"),
      TraceLog::RECORDING_MODE,
      TraceLog::RECORD_UNTIL_FULL);
  TRACE_EVENT_INSTANT0("recording", "yes", TRACE_EVENT_SCOPE_GLOBAL);
  TRACE_EVENT_INSTANT0("callback", "yes", TRACE_EVENT_SCOPE_GLOBAL);
  TraceLog::GetInstance()->SetEventCallbackDisabled();
  TRACE_EVENT_INSTANT0("recording", "yes", TRACE_EVENT_SCOPE_GLOBAL);
  TRACE_EVENT_INSTANT0("callback", "no", TRACE_EVENT_SCOPE_GLOBAL);
  EndTraceAndFlush();
  TRACE_EVENT_INSTANT0("recording", "no", TRACE_EVENT_SCOPE_GLOBAL);
  TRACE_EVENT_INSTANT0("callback", "no", TRACE_EVENT_SCOPE_GLOBAL);

  DropTracedMetadataRecords();
  ASSERT_NO_FATALS();
  VerifyCallbackAndRecordedEvents(2, 2);
}

// 2: Enable callback, enable recording, disable recording, disable callback.
TEST_F(TraceEventCallbackTest, TraceEventCallbackAndRecording2) {
  TRACE_EVENT_INSTANT0("recording", "no", TRACE_EVENT_SCOPE_GLOBAL);
  TRACE_EVENT_INSTANT0("callback", "no", TRACE_EVENT_SCOPE_GLOBAL);
  TraceLog::GetInstance()->SetEventCallbackEnabled(CategoryFilter("callback"),
                                                   Callback);
  TRACE_EVENT_INSTANT0("recording", "no", TRACE_EVENT_SCOPE_GLOBAL);
  TRACE_EVENT_INSTANT0("callback", "yes", TRACE_EVENT_SCOPE_GLOBAL);
  TraceLog::GetInstance()->SetEnabled(
      CategoryFilter("recording"),
      TraceLog::RECORDING_MODE,
      TraceLog::RECORD_UNTIL_FULL);
  TRACE_EVENT_INSTANT0("recording", "yes", TRACE_EVENT_SCOPE_GLOBAL);
  TRACE_EVENT_INSTANT0("callback", "yes", TRACE_EVENT_SCOPE_GLOBAL);
  EndTraceAndFlush();
  TRACE_EVENT_INSTANT0("recording", "no", TRACE_EVENT_SCOPE_GLOBAL);
  TRACE_EVENT_INSTANT0("callback", "yes", TRACE_EVENT_SCOPE_GLOBAL);
  TraceLog::GetInstance()->SetEventCallbackDisabled();
  TRACE_EVENT_INSTANT0("recording", "no", TRACE_EVENT_SCOPE_GLOBAL);
  TRACE_EVENT_INSTANT0("callback", "no", TRACE_EVENT_SCOPE_GLOBAL);

  DropTracedMetadataRecords();
  VerifyCallbackAndRecordedEvents(3, 1);
}

// 3: Enable recording, enable callback, disable callback, disable recording.
TEST_F(TraceEventCallbackTest, TraceEventCallbackAndRecording3) {
  TRACE_EVENT_INSTANT0("recording", "no", TRACE_EVENT_SCOPE_GLOBAL);
  TRACE_EVENT_INSTANT0("callback", "no", TRACE_EVENT_SCOPE_GLOBAL);
  TraceLog::GetInstance()->SetEnabled(
      CategoryFilter("recording"),
      TraceLog::RECORDING_MODE,
      TraceLog::RECORD_UNTIL_FULL);
  TRACE_EVENT_INSTANT0("recording", "yes", TRACE_EVENT_SCOPE_GLOBAL);
  TRACE_EVENT_INSTANT0("callback", "no", TRACE_EVENT_SCOPE_GLOBAL);
  TraceLog::GetInstance()->SetEventCallbackEnabled(CategoryFilter("callback"),
                                                   Callback);
  TRACE_EVENT_INSTANT0("recording", "yes", TRACE_EVENT_SCOPE_GLOBAL);
  TRACE_EVENT_INSTANT0("callback", "yes", TRACE_EVENT_SCOPE_GLOBAL);
  TraceLog::GetInstance()->SetEventCallbackDisabled();
  TRACE_EVENT_INSTANT0("recording", "yes", TRACE_EVENT_SCOPE_GLOBAL);
  TRACE_EVENT_INSTANT0("callback", "no", TRACE_EVENT_SCOPE_GLOBAL);
  EndTraceAndFlush();
  TRACE_EVENT_INSTANT0("recording", "no", TRACE_EVENT_SCOPE_GLOBAL);
  TRACE_EVENT_INSTANT0("callback", "no", TRACE_EVENT_SCOPE_GLOBAL);

  DropTracedMetadataRecords();
  VerifyCallbackAndRecordedEvents(1, 3);
}

// 4: Enable recording, enable callback, disable recording, disable callback.
TEST_F(TraceEventCallbackTest, TraceEventCallbackAndRecording4) {
  TRACE_EVENT_INSTANT0("recording", "no", TRACE_EVENT_SCOPE_GLOBAL);
  TRACE_EVENT_INSTANT0("callback", "no", TRACE_EVENT_SCOPE_GLOBAL);
  TraceLog::GetInstance()->SetEnabled(
      CategoryFilter("recording"),
      TraceLog::RECORDING_MODE,
      TraceLog::RECORD_UNTIL_FULL);
  TRACE_EVENT_INSTANT0("recording", "yes", TRACE_EVENT_SCOPE_GLOBAL);
  TRACE_EVENT_INSTANT0("callback", "no", TRACE_EVENT_SCOPE_GLOBAL);
  TraceLog::GetInstance()->SetEventCallbackEnabled(CategoryFilter("callback"),
                                                   Callback);
  TRACE_EVENT_INSTANT0("recording", "yes", TRACE_EVENT_SCOPE_GLOBAL);
  TRACE_EVENT_INSTANT0("callback", "yes", TRACE_EVENT_SCOPE_GLOBAL);
  EndTraceAndFlush();
  TRACE_EVENT_INSTANT0("recording", "no", TRACE_EVENT_SCOPE_GLOBAL);
  TRACE_EVENT_INSTANT0("callback", "yes", TRACE_EVENT_SCOPE_GLOBAL);
  TraceLog::GetInstance()->SetEventCallbackDisabled();
  TRACE_EVENT_INSTANT0("recording", "no", TRACE_EVENT_SCOPE_GLOBAL);
  TRACE_EVENT_INSTANT0("callback", "no", TRACE_EVENT_SCOPE_GLOBAL);

  DropTracedMetadataRecords();
  VerifyCallbackAndRecordedEvents(2, 2);
}

TEST_F(TraceEventCallbackTest, TraceEventCallbackAndRecordingDuration) {
  TraceLog::GetInstance()->SetEventCallbackEnabled(CategoryFilter("*"),
                                                   Callback);
  {
    TRACE_EVENT0("callback", "duration1");
    TraceLog::GetInstance()->SetEnabled(
        CategoryFilter("*"),
        TraceLog::RECORDING_MODE,
        TraceLog::RECORD_UNTIL_FULL);
    TRACE_EVENT0("callback", "duration2");
    EndTraceAndFlush();
    TRACE_EVENT0("callback", "duration3");
  }
  TraceLog::GetInstance()->SetEventCallbackDisabled();

  ASSERT_EQ(6u, collected_events_names_.size());
  VerifyCollectedEvent(0, TRACE_EVENT_PHASE_BEGIN, "callback", "duration1");
  VerifyCollectedEvent(1, TRACE_EVENT_PHASE_BEGIN, "callback", "duration2");
  VerifyCollectedEvent(2, TRACE_EVENT_PHASE_BEGIN, "callback", "duration3");
  VerifyCollectedEvent(3, TRACE_EVENT_PHASE_END, "callback", "duration3");
  VerifyCollectedEvent(4, TRACE_EVENT_PHASE_END, "callback", "duration2");
  VerifyCollectedEvent(5, TRACE_EVENT_PHASE_END, "callback", "duration1");
}

////////////////////////////////////////////////////////////
// Tests for synthetic delay
// (from chromium-base/debug/trace_event_synthetic_delay_unittest.cc)
////////////////////////////////////////////////////////////

namespace {

const int kTargetDurationMs = 100;
// Allow some leeway in timings to make it possible to run these tests with a
// wall clock time source too.
const int kShortDurationMs = 10;

}  // namespace

namespace debug {

class TraceEventSyntheticDelayTest : public YBTest,
                                     public TraceEventSyntheticDelayClock {
 public:
  TraceEventSyntheticDelayTest() {
    now_ = MonoTime::Min();
  }

  virtual ~TraceEventSyntheticDelayTest() {
    ResetTraceEventSyntheticDelays();
  }

  // TraceEventSyntheticDelayClock implementation.
  MonoTime Now() override {
    AdvanceTime(MonoDelta::FromMilliseconds(kShortDurationMs / 10));
    return now_;
  }

  TraceEventSyntheticDelay* ConfigureDelay(const char* name) {
    TraceEventSyntheticDelay* delay = TraceEventSyntheticDelay::Lookup(name);
    delay->SetClock(this);
    delay->SetTargetDuration(
      MonoDelta::FromMilliseconds(kTargetDurationMs));
    return delay;
  }

  void AdvanceTime(MonoDelta delta) { now_.AddDelta(delta); }

  int TestFunction() {
    MonoTime start = Now();
    { TRACE_EVENT_SYNTHETIC_DELAY("test.Delay"); }
    MonoTime end = Now();
    return narrow_cast<int>(end.GetDeltaSince(start).ToMilliseconds());
  }

  int AsyncTestFunctionBegin() {
    MonoTime start = Now();
    { TRACE_EVENT_SYNTHETIC_DELAY_BEGIN("test.AsyncDelay"); }
    MonoTime end = Now();
    return narrow_cast<int>(end.GetDeltaSince(start).ToMilliseconds());
  }

  int AsyncTestFunctionEnd() {
    MonoTime start = Now();
    { TRACE_EVENT_SYNTHETIC_DELAY_END("test.AsyncDelay"); }
    MonoTime end = Now();
    return narrow_cast<int>(end.GetDeltaSince(start).ToMilliseconds());
  }

 private:
  MonoTime now_;

  DISALLOW_COPY_AND_ASSIGN(TraceEventSyntheticDelayTest);
};

TEST_F(TraceEventSyntheticDelayTest, StaticDelay) {
  TraceEventSyntheticDelay* delay = ConfigureDelay("test.Delay");
  delay->SetMode(TraceEventSyntheticDelay::STATIC);
  EXPECT_GE(TestFunction(), kTargetDurationMs);
}

TEST_F(TraceEventSyntheticDelayTest, OneShotDelay) {
  TraceEventSyntheticDelay* delay = ConfigureDelay("test.Delay");
  delay->SetMode(TraceEventSyntheticDelay::ONE_SHOT);
  EXPECT_GE(TestFunction(), kTargetDurationMs);
  EXPECT_LT(TestFunction(), kShortDurationMs);

  delay->SetTargetDuration(
      MonoDelta::FromMilliseconds(kTargetDurationMs));
  EXPECT_GE(TestFunction(), kTargetDurationMs);
}

TEST_F(TraceEventSyntheticDelayTest, AlternatingDelay) {
  TraceEventSyntheticDelay* delay = ConfigureDelay("test.Delay");
  delay->SetMode(TraceEventSyntheticDelay::ALTERNATING);
  EXPECT_GE(TestFunction(), kTargetDurationMs);
  EXPECT_LT(TestFunction(), kShortDurationMs);
  EXPECT_GE(TestFunction(), kTargetDurationMs);
  EXPECT_LT(TestFunction(), kShortDurationMs);
}

TEST_F(TraceEventSyntheticDelayTest, AsyncDelay) {
  ConfigureDelay("test.AsyncDelay");
  EXPECT_LT(AsyncTestFunctionBegin(), kShortDurationMs);
  EXPECT_GE(AsyncTestFunctionEnd(), kTargetDurationMs / 2);
}

TEST_F(TraceEventSyntheticDelayTest, AsyncDelayExceeded) {
  ConfigureDelay("test.AsyncDelay");
  EXPECT_LT(AsyncTestFunctionBegin(), kShortDurationMs);
  AdvanceTime(MonoDelta::FromMilliseconds(kTargetDurationMs));
  EXPECT_LT(AsyncTestFunctionEnd(), kShortDurationMs);
}

TEST_F(TraceEventSyntheticDelayTest, AsyncDelayNoActivation) {
  ConfigureDelay("test.AsyncDelay");
  EXPECT_LT(AsyncTestFunctionEnd(), kShortDurationMs);
}

TEST_F(TraceEventSyntheticDelayTest, AsyncDelayNested) {
  ConfigureDelay("test.AsyncDelay");
  EXPECT_LT(AsyncTestFunctionBegin(), kShortDurationMs);
  EXPECT_LT(AsyncTestFunctionBegin(), kShortDurationMs);
  EXPECT_LT(AsyncTestFunctionEnd(), kShortDurationMs);
  EXPECT_GE(AsyncTestFunctionEnd(), kTargetDurationMs / 2);
}

TEST_F(TraceEventSyntheticDelayTest, AsyncDelayUnbalanced) {
  ConfigureDelay("test.AsyncDelay");
  EXPECT_LT(AsyncTestFunctionBegin(), kShortDurationMs);
  EXPECT_GE(AsyncTestFunctionEnd(), kTargetDurationMs / 2);
  EXPECT_LT(AsyncTestFunctionEnd(), kShortDurationMs);

  EXPECT_LT(AsyncTestFunctionBegin(), kShortDurationMs);
  EXPECT_GE(AsyncTestFunctionEnd(), kTargetDurationMs / 2);
}

TEST_F(TraceEventSyntheticDelayTest, ResetDelays) {
  ConfigureDelay("test.Delay");
  ResetTraceEventSyntheticDelays();
  EXPECT_LT(TestFunction(), kShortDurationMs);
}

TEST_F(TraceEventSyntheticDelayTest, BeginParallel) {
  TraceEventSyntheticDelay* delay = ConfigureDelay("test.AsyncDelay");
  MonoTime end_times[2];
  MonoTime start_time = Now();

  delay->BeginParallel(&end_times[0]);
  EXPECT_FALSE(!end_times[0].Initialized());

  delay->BeginParallel(&end_times[1]);
  EXPECT_FALSE(!end_times[1].Initialized());

  delay->EndParallel(end_times[0]);
  EXPECT_GE(Now().GetDeltaSince(start_time).ToMilliseconds(), kTargetDurationMs);

  start_time = Now();
  delay->EndParallel(end_times[1]);
  EXPECT_LT(Now().GetDeltaSince(start_time).ToMilliseconds(), kShortDurationMs);
}

TEST_F(TraceTest, TestVLogTrace) {
  for (FLAGS_v = 0; FLAGS_v <= 1; FLAGS_v++) {
    TraceLog* tl = TraceLog::GetInstance();
    tl->SetEnabled(CategoryFilter(CategoryFilter::kDefaultCategoryFilterString),
                   TraceLog::RECORDING_MODE,
                   TraceLog::RECORD_CONTINUOUSLY);
    VLOG_AND_TRACE("test", 1) << "hello world";
    tl->SetDisabled();
    string trace_json = TraceResultBuffer::FlushTraceLogToString();
    ASSERT_STR_CONTAINS(trace_json, "hello world");
    ASSERT_STR_CONTAINS(trace_json, "trace-test.cc");
  }
}

namespace {
string FunctionWithSideEffect(bool* b) {
  *b = true;
  return "function-result";
}
} // anonymous namespace

// Test that, if tracing is not enabled, a VLOG_AND_TRACE doesn't evaluate its
// arguments.
TEST_F(TraceTest, TestVLogTraceLazyEvaluation) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_v) = 0;
  bool function_run = false;
  VLOG_AND_TRACE("test", 1) << FunctionWithSideEffect(&function_run);
  ASSERT_FALSE(function_run);

  // If we enable verbose logging, we should run the side effect even though
  // trace logging is disabled.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_v) = 1;
  VLOG_AND_TRACE("test", 1) << FunctionWithSideEffect(&function_run);
  ASSERT_TRUE(function_run);
}

TEST_F(TraceTest, TestVLogAndEchoToConsole) {
  TraceLog* tl = TraceLog::GetInstance();
  tl->SetEnabled(CategoryFilter(CategoryFilter::kDefaultCategoryFilterString),
                 TraceLog::RECORDING_MODE,
                 TraceLog::ECHO_TO_CONSOLE);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_v) = 1;
  VLOG_AND_TRACE("test", 1) << "hello world";
  tl->SetDisabled();
}

} // namespace debug
} // namespace yb
