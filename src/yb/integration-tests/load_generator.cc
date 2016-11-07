// Copyright (c) YugaByte, Inc.

#include "yb/integration-tests/load_generator.h"

#include <gflags/gflags_declare.h>
#include <memory>
#include <queue>
#include <random>
#include <thread>

#include "cpp_redis/redis_client.hpp"
#include "cpp_redis/reply.hpp"
#include "yb/common/common.pb.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/util/atomic.h"
#include "yb/util/env.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/stopwatch.h"
#include "yb/util/subprocess.h"
#include "yb/util/threadlocal.h"

using std::atomic;
using std::atomic_bool;
using std::unique_ptr;

using strings::Substitute;

using std::shared_ptr;
using yb::Status;
using yb::ThreadPool;
using yb::ThreadPoolBuilder;
using yb::MonoDelta;
using yb::MemoryOrder;
using yb::ConditionVariable;
using yb::Mutex;
using yb::MutexLock;
using yb::CountDownLatch;
using yb::Slice;
using yb::YBPartialRow;
using yb::TableType;

using yb::client::YBClient;
using yb::client::YBError;
using yb::client::YBNoOp;
using yb::client::YBPredicate;
using yb::client::YBInsert;
using yb::client::YBSession;
using yb::client::YBScanBatch;
using yb::client::YBScanner;
using yb::client::YBRowResult;
using yb::client::YBTable;
using yb::client::YBValue;

DEFINE_bool(load_gen_verbose,
            false,
            "Custom verbose log messages for debugging the load test tool");

DEFINE_int32(load_gen_insertion_tracker_delay_ms,
             50,
             "The interval (ms) at which the load generator's \"insertion tracker thread\" "
             "wakes in up ");

DEFINE_int32(load_gen_scanner_open_retries,
             10,
             "Number of times to re-try when opening a scanner");

DEFINE_int32(load_gen_wait_time_increment_step_ms,
             100,
             "In retry loops used in the load test we increment the wait time by this number of "
             "milliseconds after every attempt.");

namespace {

void ConfigureYBSession(YBSession* session) {
  CHECK_OK(session->SetFlushMode(YBSession::FlushMode::MANUAL_FLUSH));
  session->SetTimeoutMillis(60000);
  CHECK_OK(
      session->SetExternalConsistencyMode(YBSession::ExternalConsistencyMode::CLIENT_PROPAGATED));
}

string FormatWithSize(const string& s) {
  return strings::Substitute("'$0' ($1 bytes)", s, s.size());
}

}  // namespace

namespace yb {
namespace load_generator {

string FormatHexForLoadTestKey(uint64_t x) {
  char buf[64];
  snprintf(buf, sizeof(buf) - 1, "%016zx", x);
  return buf;
}

int KeyIndexSet::NumElements() const {
  MutexLock l(mutex_);
  return set_.size();
}

void KeyIndexSet::Insert(int64_t key) {
  MutexLock l(mutex_);
  set_.insert(key);
}

bool KeyIndexSet::Contains(int64_t key) const {
  MutexLock l(mutex_);
  return set_.find(key) != set_.end();
}

bool KeyIndexSet::RemoveIfContains(int64_t key) {
  MutexLock l(mutex_);
  set<int64>::iterator it = set_.find(key);
  if (it == set_.end()) {
    return false;
  } else {
    set_.erase(it);
    return true;
  }
}

int64_t KeyIndexSet::GetRandomKey(std::mt19937_64* random_number_generator) const {
  MutexLock l(mutex_);
  // The set iterator does not support indexing, so we probabilistically choose a random element
  // by iterating the set.
  int n = set_.size();
  for (int64_t x : set_) {
    if ((*random_number_generator)() % n == 0) return x;
    --n;  // Decrement the number of remaining elements we are considering.
  }
  // This will only happen if the set is empty.
  return -1;
}

ostream& operator <<(ostream& out, const KeyIndexSet &key_index_set) {
  MutexLock l(key_index_set.mutex_);
  out << "[";
  bool first = true;
  for (auto key : key_index_set.set_) {
    if (!first) {
      out << ", ";
    }
    first = false;
    out << key;
  }
  out << "]";
  return out;
}

// ------------------------------------------------------------------------------------------------
// SessionFactory
// ------------------------------------------------------------------------------------------------

YBSessionFactory::YBSessionFactory(yb::client::YBClient* client, yb::client::YBTable* table)
    : client_(client), table_(table) {}

string YBSessionFactory::ClientId() { return client_->client_id(); }

SingleThreadedWriter* YBSessionFactory::GetWriter(MultiThreadedWriter* writer, int idx) {
  return new YBSingleThreadedWriter(writer, client_, table_, idx);
}

SingleThreadedReader* YBSessionFactory::GetReader(MultiThreadedReader* reader, int idx) {
  return new YBSingleThreadedReader(reader, client_, table_, idx);
}

SingleThreadedWriter* NoopSessionFactory::GetWriter(MultiThreadedWriter* writer, int idx) {
  return new NoopSingleThreadedWriter(writer, client_, table_, idx);
}

RedisSessionFactory::RedisSessionFactory(const string& redis_server_addr)
    : redis_server_address_(redis_server_addr) {}

string RedisSessionFactory::ClientId() { return "redis_client"; }

SingleThreadedWriter* RedisSessionFactory::GetWriter(MultiThreadedWriter* writer, int idx) {
  return new RedisSingleThreadedWriter(writer, redis_server_address_, idx);
}

SingleThreadedReader* RedisSessionFactory::GetReader(MultiThreadedReader* reader, int idx) {
  return new RedisSingleThreadedReader(reader, redis_server_address_, idx);
}

SingleThreadedWriter* RedisNoopSessionFactory::GetWriter(MultiThreadedWriter* writer, int idx) {
  return new RedisNoopSingleThreadedWriter(writer, redis_server_address_, idx);
}
// ------------------------------------------------------------------------------------------------
// MultiThreadedAction
// ------------------------------------------------------------------------------------------------

MultiThreadedAction::MultiThreadedAction(
    const string& description, int64_t num_keys, int64_t start_key, int num_action_threads,
    int num_extra_threads, SessionFactory* session_factory, atomic_bool* stop_requested_flag,
    int value_size)
    : description_(description),
      num_keys_(num_keys),
      start_key_(start_key),
      num_action_threads_(num_action_threads),
      session_factory_(session_factory),
      running_threads_latch_(num_action_threads),
      stop_requested_(stop_requested_flag),
      value_size_(value_size) {
  ThreadPoolBuilder(description)
      .set_max_threads(num_action_threads_ + num_extra_threads)
      .Build(&thread_pool_);
}

string MultiThreadedAction::GetKeyByIndex(int64_t key_index) {
  string key_index_str(Substitute("key$0", key_index));
  return Substitute(
      "$0_$1_$2", FormatHexForLoadTestKey(std::hash<string>()(key_index_str)), key_index_str,
      session_factory_->ClientId());
}

// Creates a human-readable string with hex characters to be used as a value in our test. This is
// deterministic based on key_index.
string MultiThreadedAction::GetValueByIndex(int64_t key_index) {
  string value;
  int64_t x = key_index;
  for (int i = 0; i < value_size_; ++i) {
    int val = static_cast<int>(x & 0xf);
    char c = static_cast<char>(val > 9 ? val - 10 + 'a' : val + '0');
    value.push_back(c);
    // Add pseudo-randomness by using the loop index.
    x = (x >> 4) * 31 + i;
  }
  return value;
}

void MultiThreadedAction::Start() {
  LOG(INFO) << "Starting " << num_action_threads_ << " " << description_ << " threads";
  CHECK_OK(thread_pool_->SubmitFunc(std::bind(&MultiThreadedAction::RunStatsThread, this)));
  for (int i = 0; i < num_action_threads_; i++) {
    CHECK_OK(thread_pool_->SubmitFunc(std::bind(&MultiThreadedAction::RunActionThread, this, i)));
  }
}

void MultiThreadedAction::WaitForCompletion() {
  thread_pool_->Wait();
}

// ------------------------------------------------------------------------------------------------
// MultiThreadedWriter
// ------------------------------------------------------------------------------------------------

MultiThreadedWriter::MultiThreadedWriter(
    int64_t num_keys, int64_t start_key, int num_writer_threads, SessionFactory* session_factory,
    atomic_bool* stop_flag, int value_size, int max_num_write_errors)
    : MultiThreadedAction(
          "writers", num_keys, start_key, num_writer_threads, 2, session_factory, stop_flag,
          value_size),
      next_key_(start_key),
      inserted_up_to_inclusive_(-1),
      max_num_write_errors_(max_num_write_errors) {}

void MultiThreadedWriter::Start() {
  MultiThreadedAction::Start();
  CHECK_OK(
      thread_pool_->SubmitFunc(std::bind(&MultiThreadedWriter::RunInsertionTrackerThread, this)));
}

void MultiThreadedWriter::WaitForCompletion() {
  MultiThreadedAction::WaitForCompletion();
  LOG(INFO) << "Inserted up to and including " << inserted_up_to_inclusive_.load();
}

void MultiThreadedWriter::RunActionThread(int writer_index) {
  unique_ptr<SingleThreadedWriter> writer(session_factory_->GetWriter(this, writer_index));
  writer->Run();

  LOG(INFO) << "Writer thread " << writer_index << " finished";
  running_threads_latch_.CountDown();
}

void SingleThreadedWriter::Run() {
  LOG(INFO) << "Writer thread " << writer_index_ << " started";
  ConfigureSession();
  while (!multi_threaded_writer_->IsStopRequested()) {
    int64_t key_index = multi_threaded_writer_->next_key_++;
    if (key_index >= multi_threaded_writer_->num_keys_) {
      break;
    }

    string key_str(multi_threaded_writer_->GetKeyByIndex(key_index));
    string value_str(multi_threaded_writer_->GetValueByIndex(key_index));

    if (Write(key_index, key_str, value_str)) {
      multi_threaded_writer_->inserted_keys_.Insert(key_index);
    } else {
      multi_threaded_writer_->failed_keys_.Insert(key_index);
      HandleInsertionFailure(key_index, key_str);
      if (multi_threaded_writer_->num_write_errors() >
          multi_threaded_writer_->max_num_write_errors_) {
        LOG(ERROR) << "Exceeded the maximum number of write errors "
                   << multi_threaded_writer_->max_num_write_errors_ << ", stopping the test.";
        multi_threaded_writer_->Stop();
        break;
      }
    }
  }
  CloseSession();
}

void RedisSingleThreadedWriter::ConfigureSession() {
  client_.reset(new RedisClient);
  Sockaddr remote;
  remote.ParseString(redis_server_address_, 6379);
  client_->connect(remote.host(), remote.port(), [](RedisClient&) {
    LOG(ERROR) << "client disconnected (disconnection handler)" << std::endl;
  });
}

bool RedisSingleThreadedWriter::Write(
    int64_t key_index, const string& key_str, const string& value_str) {
  bool success = false;
  auto* multi_writer = multi_threaded_writer_;
  auto writer_index = writer_index_;
  client_->set(
      key_str, value_str, [&success, multi_writer, key_index, writer_index](RedisReply& reply) {
        if ("OK" == reply.as_string()) {
          VLOG(2) << "Writer " << writer_index << " Successfully inserted key #" << key_index
                  << " into redis ";
          success = true;
        } else {
          VLOG(1) << "Failed Insersion key #" << key_index << reply.as_string();
          success = false;
        }
      });
  client_->sync_commit();

  return success;
}

void RedisSingleThreadedWriter::HandleInsertionFailure(int64_t key_index, const string& key_str) {
  // Nothing special to do for Redis failures.
}

void RedisSingleThreadedWriter::CloseSession() { client_->disconnect(); }

bool RedisNoopSingleThreadedWriter::Write(
    int64_t key_index, const string& key_str, const string& value_str) {
  bool success = false;
  auto writer_index = writer_index_;
  client_->echo("OK", [&success, key_index, writer_index](RedisReply& reply) {
    if ("OK" == reply.as_string()) {
      VLOG(2) << "Writer " << writer_index << " Successfully inserted key #" << key_index
              << " into redis ";
      success = true;
    } else {
      VLOG(1) << "Failed Insersion key #" << key_index << reply.as_string();
      success = false;
    }
  });
  client_->sync_commit();

  return success;
}

void YBSingleThreadedWriter::ConfigureSession() {
  session_ = client_->NewSession();
  ConfigureYBSession(session_.get());
}

bool YBSingleThreadedWriter::Write(
    int64_t key_index, const string& key_str, const string& value_str) {
  shared_ptr<YBInsert> insert(table_->NewInsert());
  // Generate a Put for key_str, value_str
  CHECK_OK(insert->mutable_row()->SetBinary("k", key_str.c_str()));
  CHECK_OK(insert->mutable_row()->SetBinary("v", value_str.c_str()));
  // submit a the put to apply.
  // If successful, add to inserted
  Status apply_status = session_->Apply(insert);
  if (apply_status.ok()) {
    Status flush_status = session_->Flush();
    if (flush_status.ok()) {
      multi_threaded_writer_->inserted_keys_.Insert(key_index);
      VLOG(2) << "Successfully inserted key #" << key_index << " at timestamp "
              << client_->GetLatestObservedTimestamp() << " or earlier";
    } else {
      LOG(WARNING) << "Error inserting key '" << key_str << "': "
                   << "Flush() failed"
                   << " (" << flush_status.ToString() << ")";
      return false;
    }
  } else {
    LOG(WARNING) << "Error inserting key '" << key_str << "': "
                 << "Apply() failed"
                 << " (" << apply_status.ToString() << ")";
    return false;
  }

  return true;
}

void YBSingleThreadedWriter::HandleInsertionFailure(int64_t key_index, const string& key_str) {
  vector<YBError*> errors;
  bool overflowed;
  ElementDeleter d(&errors);
  if (session_ != nullptr) {
    session_->GetPendingErrors(&errors, &overflowed);
    for (const auto error : errors) {
      LOG(WARNING) << "Explicit error while inserting: " << error->status().ToString();
    }
  }
}

void YBSingleThreadedWriter::CloseSession() { CHECK_OK(session_->Close()); }

void MultiThreadedWriter::RunStatsThread() {
  MicrosecondsInt64 prev_time = GetMonoTimeMicros();
  int64_t prev_writes = 0;
  while (!IsStopRequested() && running_threads_latch_.count() > 0) {
    running_threads_latch_.WaitFor(MonoDelta::FromSeconds(5));
    int64_t num_writes = this->num_writes();
    MicrosecondsInt64 current_time = GetMonoTimeMicros();
    LOG(INFO) << "Wrote " << num_writes << " rows ("
              << (num_writes - prev_writes) * 1000000.0 / (current_time - prev_time)
              << " writes/sec), contiguous insertion point: " << inserted_up_to_inclusive_.load()
              << ", write errors: " << failed_keys_.NumElements();
    prev_writes = num_writes;
    prev_time = current_time;
  }
}

void MultiThreadedWriter::RunInsertionTrackerThread() {
  LOG(INFO) << "Insertion tracker thread started";
  int64_t current_key = 0;  // the first key to be inserted
  while (!IsStopRequested() && running_threads_latch_.count() > 0) {
    while (failed_keys_.Contains(current_key) || inserted_keys_.RemoveIfContains(current_key)) {
      VLOG(2) << "Advancing insertion tracker to key #" << current_key;
      inserted_up_to_inclusive_.store(current_key);
      current_key++;
    }
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_load_gen_insertion_tracker_delay_ms));
  }
  LOG(INFO) << "Insertion tracker thread stopped";
}

// ------------------------------------------------------------------------------------------------
// SingleThreadedScanner
// ------------------------------------------------------------------------------------------------

SingleThreadedScanner::SingleThreadedScanner(YBTable* table)
    : table_(table),
      num_rows_(0) {
}

int64_t SingleThreadedScanner::CountRows() {
  YBScanner scanner(table_);
  CHECK_OK(scanner.Open());
  vector<YBRowResult> results;
  while (scanner.HasMoreRows()) {
    CHECK_OK(scanner.NextBatch(&results));
    num_rows_ += results.size();
    results.clear();
  }

  LOG(INFO) << " num read rows = " << num_rows_;
  return num_rows_;
}

// ------------------------------------------------------------------------------------------------
// MultiThreadedReader
// ------------------------------------------------------------------------------------------------

MultiThreadedReader::MultiThreadedReader(int64_t num_keys, int num_reader_threads,
                                         SessionFactory* session_factory,
                                         atomic<int64_t>* insertion_point,
                                         const KeyIndexSet* inserted_keys,
                                         const KeyIndexSet* failed_keys, atomic_bool* stop_flag,
                                         int value_size, int max_num_read_errors,
                                         int retries_on_empty_read)
    : MultiThreadedAction(
          "readers", num_keys, 0, num_reader_threads, 1, session_factory, stop_flag, value_size),
      insertion_point_(insertion_point),
      inserted_keys_(inserted_keys),
      failed_keys_(failed_keys),
      num_reads_(0),
      num_read_errors_(0),
      max_num_read_errors_(max_num_read_errors),
      retries_on_empty_read_(retries_on_empty_read) {}

void MultiThreadedReader::RunActionThread(int reader_index) {
  unique_ptr<SingleThreadedReader> reader_loop(session_factory_->GetReader(this, reader_index));
  reader_loop->Run();

  LOG(INFO) << "Reader thread " << reader_index << " finished";
  running_threads_latch_.CountDown();
}

void MultiThreadedReader::RunStatsThread() {
  MicrosecondsInt64 prev_time = GetMonoTimeMicros();
  int64_t prev_rows_read = 0;
  while (!IsStopRequested() && running_threads_latch_.count() > 0) {
    running_threads_latch_.WaitFor(MonoDelta::FromSeconds(5));
    MicrosecondsInt64 current_time = GetMonoTimeMicros();
    int64_t num_rows_read = num_reads_.load();
    LOG(INFO) << "Read " << num_rows_read << " rows ("
              << (num_rows_read - prev_rows_read) * 1000000.0 / (current_time - prev_time)
              << " reads/sec), read errors: " << num_read_errors_.load();
    prev_rows_read = num_rows_read;
    prev_time = current_time;
  }
}

void MultiThreadedReader::IncrementReadErrorCount() {
  if (++num_read_errors_ > max_num_read_errors_) {
    LOG(ERROR) << "Exceeded the maximum number of read errors (" << max_num_read_errors_ << ")!";
    Stop();
  }
}

void RedisSingleThreadedReader::ConfigureSession() {
  client_.reset(new RedisClient);
  Sockaddr remote;
  remote.ParseString(redis_server_address_, 6379);
  client_->connect(remote.host(), remote.port(), [](RedisClient&) {
    LOG(ERROR) << "client disconnected (disconnection handler)" << std::endl;
  });
}

void RedisSingleThreadedReader::CloseSession() { client_->disconnect(); }

void YBSingleThreadedReader::ConfigureSession() {
  session_ = client_->NewSession();
  ConfigureYBSession(session_.get());
}

bool NoopSingleThreadedWriter::Write(
    int64_t key_index, const string& key_str, const string& value_str) {
  YBNoOp noop(table_);
  gscoped_ptr<YBPartialRow> row(table_->schema().NewRow());
  CHECK_OK(row->SetBinary("k", key_str));
  Status s = noop.Execute(*row);
  if (s.ok()) {
    return true;
  }
  LOG(ERROR) << "NoOp failed" << s.CodeAsString();
  return false;
}

ReadStatus YBSingleThreadedReader::PerformRead(
    int64_t key_index, const string& key_str, const string& expected_value_str) {
  uint64_t read_ts = client_->GetLatestObservedTimestamp();
  YBScanner scanner(table_);
  CHECK_OK(scanner.SetSelection(YBClient::ReplicaSelection::LEADER_ONLY));
  CHECK_OK(scanner.SetProjectedColumns({ "k", "v" }));
  CHECK_OK(scanner.AddConjunctPredicate(
      table_->NewComparisonPredicate("k", YBPredicate::EQUAL, YBValue::CopyString(key_str))));

  Status scanner_open_status = scanner.Open();
  for (int i = 1;
       i <= FLAGS_load_gen_scanner_open_retries && !scanner_open_status.ok();
       ++i) {
    LOG(ERROR) << "Failed to open scanner: " << scanner_open_status.ToString() << ", re-trying.";
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_load_gen_wait_time_increment_step_ms * i));
    scanner_open_status = scanner.Open();
  }
  CHECK_OK(scanner_open_status);

  if (!scanner.HasMoreRows()) {
    LOG(ERROR) << "No rows found for key #" << key_index << " (read timestamp: " << read_ts << ")";
    // We don't increment the read error count here because the caller may retry up to the
    // configured number of times in this case.
    return ReadStatus::NO_ROWS;
  }

  vector<YBScanBatch::RowPtr> rows;
  scanner.NextBatch(&rows);
  if (rows.size() != 1) {
    LOG(ERROR) << "Found an invalid number of rows for key #" << key_index << ": " << rows.size()
               << " (expected to find 1 row), read timestamp: " << read_ts;
    multi_threaded_reader_->IncrementReadErrorCount();
    return ReadStatus::OTHER_ERROR;
  }

  Slice returned_key, returned_value;
  CHECK_OK(rows.front().GetBinary("k", &returned_key));
  if (returned_key != key_str) {
    LOG(ERROR) << "Invalid key returned by the read operation: '" << returned_key << "', "
               << "expected: '" << key_str << "', read timestamp: " << read_ts;
    multi_threaded_reader_->IncrementReadErrorCount();
    return ReadStatus::OTHER_ERROR;
  }

  CHECK_OK(rows.front().GetBinary("v", &returned_value));
  if (returned_value != expected_value_str) {
    LOG(ERROR) << "Invalid value returned by the read operation for key '" << key_str
               << "': " << FormatWithSize(returned_value.ToString())
               << ", expected: " << FormatWithSize(expected_value_str)
               << ", read timestamp: " << read_ts;
    multi_threaded_reader_->IncrementReadErrorCount();
    return ReadStatus::OTHER_ERROR;
  }

  return ReadStatus::OK;
}

void YBSingleThreadedReader::CloseSession() { CHECK_OK(session_->Close()); }

ReadStatus RedisSingleThreadedReader::PerformRead(
    int64_t key_index, const string& key_str, const string& expected_value_str) {
  string value_str;
  client_->get(key_str, [&value_str](RedisReply& reply) { value_str = reply.as_string(); });
  VLOG(3) << "Trying to read key #" << key_index << " from redis "
          << " key : " << key_str;
  client_->sync_commit();

  if (expected_value_str != value_str) {
    VLOG(1) << "Read the wrong value for #" << key_index << " from redis "
            << " key : " << key_str << " value : " << value_str
            << " expected : " << expected_value_str;
    return ReadStatus::OTHER_ERROR;
  }

  VLOG(2) << "Reader " << reader_index_ << " Successfully read key #" << key_index << " from redis "
          << " key : " << key_str << " value : " << value_str;
  return ReadStatus::OK;
}

void SingleThreadedReader::Run() {
  std::mt19937_64 random_number_generator(reader_index_);

  LOG(INFO) << "Reader thread " << reader_index_ << " started";
  ConfigureSession();

  // Wait until at least one row has been inserted (keys are numbered starting from 1).
  while (!multi_threaded_reader_->IsStopRequested() &&
         multi_threaded_reader_->insertion_point_->load() < 0) {
    VLOG(1) << "Reader thread " << reader_index_ << " Sleeping until load() >= 0";
    SleepFor(MonoDelta::FromMilliseconds(10));
  }

  while (!multi_threaded_reader_->IsStopRequested()) {
    int64_t key_index = NextKeyIndexToRead(&random_number_generator);

    ++multi_threaded_reader_->num_reads_;
    string key_str(multi_threaded_reader_->GetKeyByIndex(key_index));
    string expected_value_str(multi_threaded_reader_->GetValueByIndex(key_index));
    ReadStatus read_status = PerformRead(key_index, key_str, expected_value_str);

    // We support a mode in which we don't treat a read operation returning zero rows as an error
    // for up to a configured number of attempts. This is because a new leader might not yet be able
    // to serve up-to-date data for raw RocksDB-backed tables with no additional MVCC.
    // See https://yugabyte.atlassian.net/browse/ENG-115 for details.
    for (int i = 1;
         i <= multi_threaded_reader_->retries_on_empty_read_ && read_status == ReadStatus::NO_ROWS;
         ++i) {
      SleepFor(MonoDelta::FromMilliseconds(i * FLAGS_load_gen_wait_time_increment_step_ms));
      read_status = PerformRead(key_index, key_str, expected_value_str);
    }

    if (read_status != ReadStatus::OK) multi_threaded_reader_->IncrementReadErrorCount();
  }

  CloseSession();
}

int64_t SingleThreadedReader::NextKeyIndexToRead(std::mt19937_64* random_number_generator) const {
  int64_t key_index = 0;
  VLOG(3) << "Reader thread " << reader_index_ << " waiting to load insertion point";
  int64_t written_up_to = multi_threaded_reader_->insertion_point_->load();
  do {
        VLOG(3) << "Reader thread " << reader_index_ << " coin toss";
        switch ((*random_number_generator)() % 3) {
          case 0:
            // Read the latest value that the insertion tracker knows we've written up to.
            key_index = written_up_to;
            break;
          case 1:
            // Read one of the keys that have been successfully inserted but have not been processed
            // by the insertion tracker thread yet.
            key_index =
                multi_threaded_reader_->inserted_keys_->GetRandomKey(random_number_generator);
            if (key_index == -1) {
              // The set is empty.
              key_index = written_up_to;
            }
            break;

          default:
            // We're assuming the total number of keys is < RAND_MAX (~2 billion) here.
            key_index = (*random_number_generator)() % (written_up_to + 1);
            break;
        }
        // Ensure we don't try to read a key for which a write failed.
      } while (multi_threaded_reader_->failed_keys_->Contains(key_index) &&
               !multi_threaded_reader_->IsStopRequested());

  VLOG(1) << "Reader thread " << reader_index_ << " saw written_up_to=" << written_up_to
          << " and picked key #" << key_index;
  return key_index;
}

}  // namespace load_generator
}  // namespace yb
