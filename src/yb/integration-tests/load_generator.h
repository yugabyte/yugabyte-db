// Copyright (c) YugaByte, Inc.

#ifndef YB_INTEGRATION_TESTS_LOAD_GENERATOR_H
#define YB_INTEGRATION_TESTS_LOAD_GENERATOR_H

#include <string>
#include <set>
#include <iostream>
#include <mutex>
#include <atomic>

#include "yb/client/client.h"

#include <boost/thread/mutex.hpp>

#include "yb/gutil/stl_util.h"
#include "yb/util/threadpool.h"
#include "yb/util/countdown_latch.h"

namespace yb {
namespace load_generator {

std::string FormatHexForLoadTestKey(uint64_t x);

class KeyIndexSet {
 public:
  int NumElements() const;
  void Insert(int64_t key);
  bool Contains(int64_t key) const;
  bool RemoveIfContains(int64_t key);
  int64_t GetRandomKey() const;

 private:
  std::set<int64_t> set_;
  mutable Mutex mutex_;

  friend std::ostream& operator <<(std::ostream& out, const KeyIndexSet &key_index_set);
};

class MultiThreadedAction {
 public:
  MultiThreadedAction(
    const std::string& description,
    int64_t num_keys,
    int64_t start_key,
    int num_action_threads,
    int num_extra_threads,
    yb::client::YBClient* client,
    yb::client::YBTable* table,
    std::atomic_bool* stop_requested_flag,
    int value_size);

  virtual void Start();
  virtual void WaitForCompletion();

  void Stop() { stop_requested_->store(true); }
  bool IsStopRequested() { return stop_requested_->load(); }

 protected:

  static std::string GetKeyByIndex(int64_t key_index);

  // The value returned is compared as a string on read, so having a '\0' will use incorrect size.
  // This also creates a human readable string with hex characters between '0'-'f'.
  std::string GetValueByIndex(int64_t key_index);

  virtual void RunActionThread(int actionIndex) = 0;
  virtual void RunStatsThread() = 0;

  std::string description_;
  const int64_t num_keys_; // Total number of keys in the table after successful end of this action
  const int64_t start_key_; // First insertion key index of the write action
  const int num_action_threads_;
  yb::client::YBClient* client_;
  yb::client::YBTable* table_;

  std::unique_ptr<ThreadPool> thread_pool_;
  yb::CountDownLatch running_threads_latch_;

  std::atomic_bool* const stop_requested_;

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
      int64_t num_keys,
      int64_t start_key,
      int num_writer_threads,
      yb::client::YBClient* client,
      yb::client::YBTable* table,
      std::atomic_bool* stop_flag,
      int value_size,
      int max_num_write_errors);

  virtual void Start() override;
  std::atomic<int64_t>* InsertionPoint() { return &inserted_up_to_inclusive_; }
  const KeyIndexSet* InsertedKeys() const { return &inserted_keys_; }
  const KeyIndexSet* FailedKeys() const { return &failed_keys_; }

  int64_t num_writes() { return next_key_.load() - start_key_; }
  int num_write_errors() { return failed_keys_.NumElements(); }

 private:
  virtual void RunActionThread(int writerIndex) override;
  virtual void RunStatsThread() override;
  void RunInsertionTrackerThread();

  // Returns true if the calling writer thread should stop.
  bool HandleInsertionFailure(int64_t key_index, const char* const reason, Status status);

  // This is the current key to be inserted by any thread. Each thread does an atomic get and
  // increment operation and inserts the current value.
  std::atomic<int64_t> next_key_;
  std::atomic<int64_t> inserted_up_to_inclusive_;

  KeyIndexSet inserted_keys_;
  KeyIndexSet failed_keys_;

  int max_num_write_errors_;
};

// ------------------------------------------------------------------------------------------------
// SingleThreadedScanner
// ------------------------------------------------------------------------------------------------

// TODO: create a multi-threaded version of this.
class SingleThreadedScanner {
 public:
  SingleThreadedScanner(yb::client::YBTable* table);

  int64_t CountRows();

 private:
  yb::client::YBTable* table_;
  int64_t num_rows_;
};

// ------------------------------------------------------------------------------------------------
// MultiThreadedReader
// ------------------------------------------------------------------------------------------------

enum class ReadStatus { OK, NO_ROWS, OTHER_ERROR };

class MultiThreadedReader : public MultiThreadedAction {
 public:
  MultiThreadedReader(
      int64 num_keys,
      int num_reader_threads,
      yb::client::YBClient* client,
      yb::client::YBTable* table,
      std::atomic<int64_t>* insertion_point,
      const KeyIndexSet* inserted_keys,
      const KeyIndexSet* failed_keys,
      std::atomic_bool* stop_flag,
      int value_size,
      int max_num_read_errors,
      int retries_on_empty_read);

  void IncrementReadErrorCount();

  int64_t num_reads() { return num_reads_; }
  int64_t num_read_errors() { return num_read_errors_.load(); }

 protected:
  virtual void RunActionThread(int reader_index) override;
  virtual void RunStatsThread() override;

 private:

  const std::atomic<int64_t>* insertion_point_;
  const KeyIndexSet* inserted_keys_;
  const KeyIndexSet* failed_keys_;

  ReadStatus PerformRead(int64_t key_index);

  std::atomic<int64_t> num_reads_;
  std::atomic<int64_t> num_read_errors_;
  const int max_num_read_errors_;
  const int retries_on_empty_read_;
};

}
}

#endif
