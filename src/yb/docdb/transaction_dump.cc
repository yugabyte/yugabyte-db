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

#include "yb/docdb/transaction_dump.h"

#include <condition_variable>
#include <mutex>

#include <glog/logging.h>

#include "yb/util/env.h"
#include "yb/util/lockfree.h"
#include "yb/util/path_util.h"
#include "yb/util/result.h"
#include "yb/util/status_log.h"
#include "yb/util/thread.h"

DEFINE_bool(dump_transactions, false, "Dump transactions data in debug binary format");
// The output dir is tried in the following order (first existing and non empty is taken):
// Symlink $HOME/logs/latest_test
// Flag log_dir
// /tmp

namespace yb {
namespace docdb {

namespace {

struct DumpEntry {
  DumpEntry* next = nullptr;
  size_t size;
  char data[0];
};

void SetNext(DumpEntry* entry, DumpEntry* next) {
  entry->next = next;
}

DumpEntry* GetNext(DumpEntry* entry) {
  return entry->next;
}

class Dumper {
 public:
  Dumper() {
    CHECK_OK(Thread::Create(
        "transaction_Dump", "writer_thread", &Dumper::Execute, this, &writer_thread_));
  }

  ~Dumper() {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      stop_.store(true, std::memory_order_release);
    }
    cond_.notify_one();
    if (writer_thread_) {
      writer_thread_->Join();
    }
    while (auto* entry = queue_.Pop()) {
      free(entry);
    }
  }

  void Dump(const SliceParts& parts) {
    size_t size = parts.SumSizes();
    auto* entry = static_cast<DumpEntry*>(malloc(offsetof(DumpEntry, data) + size));
    entry->size = size;
    parts.CopyAllTo(entry->data);
    queue_.Push(entry);
    cond_.notify_one();
  }

 private:
  void Execute() {
    Open();
    DumpEntry* entry = nullptr;
    for (;;) {
      while (entry) {
        CHECK_OK(file_->Append(Slice(entry->data, entry->size)));
        free(entry);
        entry = queue_.Pop();
      }
      {
        std::unique_lock<std::mutex> lock(mutex_);
        if (stop_.load(std::memory_order_acquire)) {
          break;
        }
        entry = queue_.Pop();
        if (!entry) {
          cond_.wait(lock);
        }
      }
    }
  }

  void Open() {
    std::string dir = OutDir();

    auto now = std::time(/* arg= */ nullptr);
    auto* tm = std::localtime(&now);
    char buffer[32];
    buffer[strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", tm)] = 0;
    auto fname = JoinPathSegments(dir, Format("DUMP.$0.$1", buffer, getpid()));
    LOG(INFO) << "Dump transactions to " << fname;
    CHECK_OK(Env::Default()->NewWritableFile(fname, &file_));
  }

  std::string OutDir() {
    std::string result;
    auto* home = getenv("HOME");
    if (home && home[0]) {
      auto latest_logs = JoinPathSegments(home, "logs/latest_test");
      auto result_dir = Env::Default()->ReadLink(latest_logs);
      if (result_dir.ok()) {
        result = *result_dir;
      } else {
        LOG(WARNING) << "Failed to read link " << latest_logs << ": " << result_dir.status();
      }
    }
    if (result.empty()) {
      result = FLAGS_log_dir;
    }
    if (result.empty()) {
      result = "/tmp";
    }
    return result;
  }

  std::unique_ptr<WritableFile> file_;
  scoped_refptr<Thread> writer_thread_;
  std::atomic<bool> stop_{false};
  MPSCQueue<DumpEntry> queue_;
  std::mutex mutex_;
  std::condition_variable cond_;
};

} // namespace

void TransactionDump(const SliceParts& parts) {
  static Dumper dumper;
  dumper.Dump(parts);
}

}  // namespace docdb
}  // namespace yb
