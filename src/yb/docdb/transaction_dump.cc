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

#include <zlib.h>

#include <condition_variable>
#include <mutex>

#include "yb/util/logging.h"

#include "yb/gutil/casts.h"

#include "yb/util/callsite_profiling.h"
#include "yb/util/env.h"
#include "yb/util/flags.h"
#include "yb/util/lockfree.h"
#include "yb/util/path_util.h"
#include "yb/util/size_literals.h"
#include "yb/util/status_log.h"
#include "yb/util/thread.h"

using namespace yb::size_literals;

DEFINE_RUNTIME_bool(dump_transactions, false, "Dump transactions data in debug binary format.");
// The output dir is tried in the following order (first existing and non empty is taken):
// Symlink $HOME/logs/latest_test
// Flag log_dir
// Flag tmp_dir
// /tmp

DEFINE_RUNTIME_uint64(dump_transactions_chunk_size, 10_GB,
                      "Start new transaction dump when current one reaches specified limit.");

DEFINE_RUNTIME_uint64(dump_transactions_gzip, true,
                      "Whether transaction dump should be compressed in GZip format.");

DECLARE_string(tmp_dir);

namespace yb {
namespace docdb {

namespace {

struct DumpEntry {
  DumpEntry* next = nullptr;
  size_t size;
  char data[0];
};

class DumpWriter {
 public:
  virtual ~DumpWriter() = default;

  virtual void Write(const char* data, size_t size) = 0;
};

class GZipWriter : public DumpWriter {
 public:
  explicit GZipWriter(const std::string& fname) : file_(gzopen(fname.c_str(), "wb")) {
    if (file_ == nullptr) {
      auto err = errno;
      LOG(FATAL) << "Failed to open gzip file: " << fname << ": " << err;
    }
  }

  ~GZipWriter() {
    gzclose_w(file_);
  }

  void Write(const char* data, size_t size) override {
    auto written = gzwrite(file_, data, narrow_cast<unsigned>(size));
    if (written == 0) {
      auto err = errno;
      LOG(FATAL) << "Failed to write gzip file: " << err;
    }
    CHECK_EQ(written, size);
  }
 private:
  gzFile file_;
};

class FileWriter : public DumpWriter {
 public:
  explicit FileWriter(const std::string& fname) {
    CHECK_OK(Env::Default()->NewWritableFile(fname, &file_));
  }

  void Write(const char* data, size_t size) override {
    CHECK_OK(file_->Append(Slice(data, size)));
  }
 private:
  std::unique_ptr<WritableFile> file_;
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
    YB_PROFILE(cond_.notify_one());
    if (writer_thread_) {
      writer_thread_->Join();
    }
    while (auto* entry = queue_.Pop()) {
      free(entry);
    }
    writer_ = nullptr;
  }

  void Dump(const SliceParts& parts) {
    size_t size = parts.SumSizes();
    auto* entry = static_cast<DumpEntry*>(malloc(offsetof(DumpEntry, data) + size));
    entry->size = size;
    parts.CopyAllTo(entry->data);
    queue_.Push(entry);
    YB_PROFILE(cond_.notify_one());
  }

 private:
  void Execute() {
    Open();
    DumpEntry* entry = nullptr;
    for (;;) {
      while (entry) {
        writer_->Write(entry->data, entry->size);
        current_file_size_ += entry->size;
        free(entry);
        if (current_file_size_ >= FLAGS_dump_transactions_chunk_size) {
          writer_ = nullptr;
          Open();
        }
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
    CHECK_OK(Env::Default()->CreateDirs(dir));
    if (FLAGS_dump_transactions_gzip) {
      fname += ".gz";
      writer_ = std::make_unique<GZipWriter>(fname);
    } else {
      writer_ = std::make_unique<FileWriter>(fname);
    }
    current_file_size_ = 0;
    LOG(INFO) << "Dump transactions to " << fname;
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
      result = FLAGS_tmp_dir;
    }
    return result;
  }

  std::unique_ptr<DumpWriter> writer_;
  size_t current_file_size_ = 0;
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
