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

#pragma once

#include <dirent.h>
#include <signal.h>
#include <spawn.h>

#include <atomic>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

#include "yb/client/client_fwd.h"

#include "yb/util/countdown_latch.h"
#include "yb/util/status.h"
#include "yb/util/threadpool.h"
#include "yb/util/tsan_util.h"

namespace yb {

namespace redisserver {
class RedisClient;
}

namespace load_generator {

using redisserver::RedisClient;

class SingleThreadedReader;
class SingleThreadedWriter;
class MultiThreadedReader;
class MultiThreadedWriter;

std::string FormatHexForLoadTestKey(uint64_t x);

class KeyIndexSet {
 public:
  size_t NumElements() const;
  void Insert(int64_t key);
  bool Contains(int64_t key) const;
  bool RemoveIfContains(int64_t key);
  int64_t GetRandomKey(std::mt19937_64* random_number_generator) const;

 private:
  std::set<int64_t> set_;
  mutable Mutex mutex_;

  friend std::ostream& operator <<(std::ostream& out, const KeyIndexSet &key_index_set);
};

class SessionFactory {
 public:
  virtual ~SessionFactory() {}
  virtual std::string ClientId() = 0;
  virtual SingleThreadedWriter* GetWriter(MultiThreadedWriter* writer, int idx) = 0;
  virtual SingleThreadedReader* GetReader(MultiThreadedReader* reader, int idx) = 0;
};

class YBSessionFactory : public SessionFactory {
 public:
  YBSessionFactory(yb::client::YBClient* client, yb::client::TableHandle* table);

  virtual std::string ClientId() override;
  SingleThreadedWriter* GetWriter(MultiThreadedWriter* writer, int idx) override;
  SingleThreadedReader* GetReader(MultiThreadedReader* reader, int idx) override;

 protected:
  yb::client::YBClient* const client_;
  yb::client::TableHandle* table_;
};

class NoopSessionFactory : public YBSessionFactory {
 public:
  NoopSessionFactory(yb::client::YBClient* client, yb::client::TableHandle* table)
      : YBSessionFactory(client, table) {}

  SingleThreadedWriter* GetWriter(MultiThreadedWriter* writer, int idx) override;
};

class RedisSessionFactory : public SessionFactory {
 public:
  explicit RedisSessionFactory(const std::string& redis_server_address);

  virtual std::string ClientId() override;
  SingleThreadedWriter* GetWriter(MultiThreadedWriter* writer, int idx) override;
  SingleThreadedReader* GetReader(MultiThreadedReader* reader, int idx) override;

 protected:
  std::string redis_server_addresses_;
};

class RedisNoopSessionFactory : public RedisSessionFactory {
 public:
  explicit RedisNoopSessionFactory(const std::string& redis_server_address)
      : RedisSessionFactory(redis_server_address) {}

  SingleThreadedWriter* GetWriter(MultiThreadedWriter* writer, int idx) override;
};

class MultiThreadedAction {
 public:
  MultiThreadedAction(
      const std::string& description, int64_t num_keys, int64_t start_key, int num_action_threads,
      int num_extra_threads, const std::string& client_id, std::atomic_bool* stop_requested_flag,
      int value_size);

  virtual ~MultiThreadedAction();

  virtual void Start();
  virtual void WaitForCompletion();

  void Stop() { stop_requested_->store(true); }
  bool IsStopRequested() { return stop_requested_->load(); }
  void set_client_id(const std::string& client_id) { client_id_ = client_id; }
  bool IsRunning() { return running_threads_latch_.count() > 0; }

 protected:
  friend class SingleThreadedReader;
  friend class SingleThreadedWriter;

  std::string GetKeyByIndex(int64_t key_index);

  // The value returned is compared as a string on read, so having a '\0' will use incorrect size.
  // This also creates a human readable string with hex characters between '0'-'f'.
  std::string GetValueByIndex(int64_t key_index);

  virtual void RunActionThread(int actionIndex) = 0;
  virtual void RunStatsThread() = 0;

  std::string description_;
  const int64_t num_keys_;  // Total number of keys in the table after successful end of this action
  const int64_t start_key_;  // First insertion key index of the write action
  const int num_action_threads_;
  std::string client_id_;

  std::unique_ptr<ThreadPool> thread_pool_;
  yb::CountDownLatch running_threads_latch_;

  std::atomic_bool* const stop_requested_;
  std::atomic<bool> paused_ { false };

  const int value_size_;
};

// ------------------------------------------------------------------------------------------------
// MultiThreadedWriter
// ------------------------------------------------------------------------------------------------

class MultiThreadedWriter : public MultiThreadedAction {
 public:
  virtual void WaitForCompletion() override;

  // Client and table are managed by the caller and their lifetime should be a superset of this
  // object's lifetime.
  MultiThreadedWriter(
      int64_t num_keys, int64_t start_key, int num_writer_threads, SessionFactory* session_factory,
      std::atomic<bool>* stop_flag, int value_size, size_t max_num_write_errors);

  void Start() override;
  std::atomic<int64_t>* InsertionPoint() { return &inserted_up_to_inclusive_; }
  const KeyIndexSet* InsertedKeys() const { return &inserted_keys_; }
  const KeyIndexSet* FailedKeys() const { return &failed_keys_; }

  int64_t num_writes() { return next_key_.load() - start_key_; }
  size_t num_write_errors() { return failed_keys_.NumElements(); }
  void AssertSucceeded() { ASSERT_EQ(num_write_errors(), 0); }

  void set_pause_flag(std::atomic<bool>* pause_flag) { pause_flag_ = pause_flag; }

 private:
  friend class SingleThreadedWriter;
  friend class RedisSingleThreadedWriter;
  friend class YBSingleThreadedWriter;

  virtual void RunActionThread(int writerIndex) override;
  virtual void RunStatsThread() override;
  void RunInsertionTrackerThread();

  KeyIndexSet inserted_keys_;
  KeyIndexSet failed_keys_;
  SessionFactory* session_factory_;

  // This is the current key to be inserted by any thread. Each thread does an atomic get and
  // increment operation and inserts the current value.
  std::atomic<int64_t> next_key_;
  std::atomic<int64_t> inserted_up_to_inclusive_;

  size_t max_num_write_errors_ = 0;
  std::atomic<bool>* pause_flag_ = nullptr;
};

class SingleThreadedWriter {
 public:
  SingleThreadedWriter(MultiThreadedWriter* writer, int writer_index)
      : multi_threaded_writer_(writer), writer_index_(writer_index) {}
  virtual ~SingleThreadedWriter() {}
  void set_pause_flag(std::atomic<bool>* pause_flag) { pause_flag_ = pause_flag; }
  void Run();

 protected:
  MultiThreadedWriter* multi_threaded_writer_;
  const int writer_index_;

 private:
  virtual bool Write(int64_t key_index, const std::string& key_str, const std::string& value_str) = 0;
  virtual void ConfigureSession() = 0;
  virtual void CloseSession() = 0;

  // Returns true if the calling writer thread should stop.
  virtual void HandleInsertionFailure(int64_t key_index, const std::string& key_str) = 0;

  std::atomic<bool>* pause_flag_ = nullptr;
};

class YBSingleThreadedWriter : public SingleThreadedWriter {
 public:
  YBSingleThreadedWriter(
      MultiThreadedWriter* writer, client::YBClient* client, client::TableHandle* table,
      int writer_index)
      : SingleThreadedWriter(writer, writer_index), client_(client), table_(table) {}

 protected:
  client::YBClient* const client_;
  client::TableHandle* table_;
  std::shared_ptr<client::YBSession> session_;

 private:
  virtual bool Write(int64_t key_index, const std::string& key_str, const std::string& value_str) override;
  virtual void ConfigureSession() override;
  virtual void CloseSession() override;
  virtual void HandleInsertionFailure(int64_t key_index, const std::string& key_str) override;
};

class NoopSingleThreadedWriter : public YBSingleThreadedWriter {
 public:
  NoopSingleThreadedWriter(
      MultiThreadedWriter* reader, client::YBClient* client, client::TableHandle* table,
      int reader_index) : YBSingleThreadedWriter(reader, client, table, reader_index) {}

 private:
  virtual bool Write(int64_t key_index, const std::string& key_str, const std::string& value_str) override;
};

class RedisSingleThreadedWriter : public SingleThreadedWriter {
 public:
  RedisSingleThreadedWriter(
      MultiThreadedWriter* writer, std::string redis_server_addrs, int writer_index)
      : SingleThreadedWriter(writer, writer_index), redis_server_addresses_(redis_server_addrs) {}

 protected:
  virtual bool Write(int64_t key_index, const std::string& key_str, const std::string& value_str) override;
  virtual void ConfigureSession() override;
  virtual void CloseSession() override;
  virtual void HandleInsertionFailure(int64_t key_index, const std::string& key_str) override;

  std::vector<std::shared_ptr<RedisClient>> clients_;
  const std::string redis_server_addresses_;
};

class RedisNoopSingleThreadedWriter : public RedisSingleThreadedWriter {
 public:
  RedisNoopSingleThreadedWriter(
      MultiThreadedWriter* writer, std::string redis_server_addr, int writer_index)
      : RedisSingleThreadedWriter(writer, redis_server_addr, writer_index) {}

 protected:
  virtual bool Write(int64_t key_index, const std::string& key_str, const std::string& value_str) override;
};
// ------------------------------------------------------------------------------------------------
// SingleThreadedScanner
// ------------------------------------------------------------------------------------------------

// TODO: create a multi-threaded version of this.
class SingleThreadedScanner {
 public:
  explicit SingleThreadedScanner(client::TableHandle* table);

  int64_t CountRows();

 private:
  client::TableHandle* table_;
};

// ------------------------------------------------------------------------------------------------
// MultiThreadedReader
// ------------------------------------------------------------------------------------------------

YB_DEFINE_ENUM(ReadStatus, (kOk)(kNoRows)(kInvalidRead)(kExtraRows)(kOtherError));
YB_DEFINE_ENUM(MultiThreadedReaderOption, (kStopOnEmptyRead)(kStopOnExtraRead)(kStopOnInvalidRead));
using MultiThreadedReaderOptions = EnumBitSet<MultiThreadedReaderOption>;

class MultiThreadedReader : public MultiThreadedAction {
 public:
  MultiThreadedReader(int64_t num_keys, int num_reader_threads,
                      SessionFactory* session_factory,
                      std::atomic<int64_t>* insertion_point,
                      const KeyIndexSet* inserted_keys,
                      const KeyIndexSet* failed_keys,
                      std::atomic<bool>* stop_flag, int value_size,
                      size_t max_num_read_errors,
                      MultiThreadedReaderOptions options = MultiThreadedReaderOptions{
                          MultiThreadedReaderOption::kStopOnEmptyRead,
                          MultiThreadedReaderOption::kStopOnExtraRead,
                          MultiThreadedReaderOption::kStopOnInvalidRead});

  void IncrementReadErrorCount(ReadStatus read_status);

  int64_t num_reads() { return num_reads_; }
  size_t num_read_errors() { return num_read_errors_.load(); }

  // Returns read status that caused stop of the reader.
  ReadStatus read_status_stopped() { return read_status_stopped_; }

  void AssertSucceeded() { ASSERT_EQ(num_read_errors(), 0); }

 protected:
  virtual void RunActionThread(int reader_index) override;
  virtual void RunStatsThread() override;

 private:
  friend class SingleThreadedReader;
  friend class YBSingleThreadedReader;

  SessionFactory* session_factory_;
  const std::atomic<int64_t>* insertion_point_;
  const KeyIndexSet* inserted_keys_;
  const KeyIndexSet* failed_keys_;

  std::atomic<int64_t> num_reads_;
  std::atomic<size_t> num_read_errors_;
  const size_t max_num_read_errors_;
  MultiThreadedReaderOptions options_;
  ReadStatus read_status_stopped_ = ReadStatus::kOk;
};

class SingleThreadedReader {
 public:
  SingleThreadedReader(MultiThreadedReader* reader, int reader_index)
      : multi_threaded_reader_(reader), reader_index_(reader_index) {}
  virtual ~SingleThreadedReader() {}

  void Run();

 protected:
  MultiThreadedReader* multi_threaded_reader_;
  const int reader_index_;

 private:
  virtual ReadStatus PerformRead(
      int64_t key_index, const std::string& key_str, const std::string& expected_value) = 0;
  virtual void ConfigureSession() = 0;
  virtual void CloseSession() = 0;

  int64_t NextKeyIndexToRead(std::mt19937_64* random_number_generator) const;
};

class YBSingleThreadedReader : public SingleThreadedReader {
 public:
  YBSingleThreadedReader(
      MultiThreadedReader* reader, client::YBClient* client, client::TableHandle* table,
      int reader_index)
      : SingleThreadedReader(reader, reader_index), client_(client), table_(table) {}

 protected:
  client::YBClient* const client_;
  client::TableHandle* table_;
  std::shared_ptr<client::YBSession> session_;

 private:
  virtual ReadStatus PerformRead(
      int64_t key_index, const std::string& key_str, const std::string& expected_value) override;
  virtual void ConfigureSession() override;
  virtual void CloseSession() override;

};

class RedisSingleThreadedReader : public SingleThreadedReader {
 public:
  RedisSingleThreadedReader(MultiThreadedReader* reader, std::string redis_server_addr, int reader_index)
      : SingleThreadedReader(reader, reader_index), redis_server_addresses_(redis_server_addr) {}

 private:
  virtual ReadStatus PerformRead(
      int64_t key_index, const std::string& key_str, const std::string& expected_value) override;
  virtual void ConfigureSession() override;
  virtual void CloseSession() override;

  std::vector<std::shared_ptr<RedisClient>> clients_;
  const std::string redis_server_addresses_;
};

}  // namespace load_generator
}  // namespace yb
