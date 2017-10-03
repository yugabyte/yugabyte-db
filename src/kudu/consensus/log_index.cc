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

// The implementation of the Log Index.
//
// The log index is implemented by a set of on-disk files, each containing a fixed number
// (kEntriesPerIndexChunk) of fixed size entries. Each index chunk is numbered such that,
// for a given log index, we can determine which chunk contains its index entry by a
// simple division operation. Because the entries are fixed size, we can compute the
// index offset by a modulo.
//
// When the log is GCed, we remove any index chunks which are no longer needed, and
// unmap them.

#include "kudu/consensus/log_index.h"

#include <fcntl.h>
#include <string>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "kudu/consensus/opid_util.h"
#include "kudu/util/locks.h"
#include "kudu/gutil/map-util.h"
#include "kudu/gutil/stringprintf.h"
#include "kudu/gutil/strings/substitute.h"

using std::string;
using strings::Substitute;

#define RETRY_ON_EINTR(ret, expr) do { \
  ret = expr; \
} while ((ret == -1) && (errno == EINTR));

namespace kudu {
namespace log {

// The actual physical entry in the file.
// This mirrors LogIndexEntry but uses simple primitives only so we can
// read/write it via mmap.
// See LogIndexEntry for docs.
struct PhysicalEntry {
  int64_t term;
  uint64_t segment_sequence_number;
  uint64_t offset_in_segment;
} PACKED;

static const int64_t kEntriesPerIndexChunk = 1000000;
static const int64_t kChunkFileSize = kEntriesPerIndexChunk * sizeof(PhysicalEntry);

////////////////////////////////////////////////////////////
// LogIndex::IndexChunk implementation
////////////////////////////////////////////////////////////

// A single chunk of the index, representing a fixed number of entries.
// This class maintains the open file descriptor and mapped memory.
class LogIndex::IndexChunk : public RefCountedThreadSafe<LogIndex::IndexChunk> {
 public:
  explicit IndexChunk(string path);
  ~IndexChunk();

  // Open and map the memory.
  Status Open();
  void GetEntry(int entry_index, PhysicalEntry* ret);
  void SetEntry(int entry_index, const PhysicalEntry& entry);

 private:
  const string path_;
  int fd_;
  uint8_t* mapping_;
};

namespace  {
Status CheckError(int rc, const char* operation) {
  if (PREDICT_FALSE(rc < 0)) {
    int err = errno;
    return Status::IOError(operation, ErrnoToString(err), err);
  }
  return Status::OK();
}
} // anonymous namespace

LogIndex::IndexChunk::IndexChunk(std::string path)
    : path_(std::move(path)), fd_(-1), mapping_(nullptr) {}

LogIndex::IndexChunk::~IndexChunk() {
  if (mapping_ != nullptr) {
    munmap(mapping_, kChunkFileSize);
  }

  if (fd_ >= 0) {
    close(fd_);
  }
}

Status LogIndex::IndexChunk::Open() {
  RETRY_ON_EINTR(fd_, open(path_.c_str(), O_CLOEXEC | O_CREAT | O_RDWR, 0666));
  RETURN_NOT_OK(CheckError(fd_, "open"));

  int err;
  RETRY_ON_EINTR(err, ftruncate(fd_, kChunkFileSize));
  RETURN_NOT_OK(CheckError(fd_, "truncate"));

  mapping_ = static_cast<uint8_t*>(mmap(nullptr, kChunkFileSize, PROT_READ | PROT_WRITE,
                                        MAP_SHARED, fd_, 0));
  if (mapping_ == nullptr) {
    int err = errno;
    return Status::IOError("Unable to mmap()", ErrnoToString(err), err);
  }

  return Status::OK();
}

void LogIndex::IndexChunk::GetEntry(int entry_index, PhysicalEntry* ret) {
  DCHECK_GE(fd_, 0) << "Must Open() first";
  DCHECK_LT(entry_index, kEntriesPerIndexChunk);

  memcpy(ret, mapping_ + sizeof(PhysicalEntry) * entry_index, sizeof(PhysicalEntry));
}

void LogIndex::IndexChunk::SetEntry(int entry_index, const PhysicalEntry& phys) {
  DCHECK_GE(fd_, 0) << "Must Open() first";
  DCHECK_LT(entry_index, kEntriesPerIndexChunk);

  memcpy(mapping_ + sizeof(PhysicalEntry) * entry_index, &phys, sizeof(PhysicalEntry));
}

////////////////////////////////////////////////////////////
// LogIndex
////////////////////////////////////////////////////////////

LogIndex::LogIndex(std::string base_dir) : base_dir_(std::move(base_dir)) {}

LogIndex::~LogIndex() {
}

string LogIndex::GetChunkPath(int64_t chunk_idx) {
  return StringPrintf("%s/index.%09" PRId64, base_dir_.c_str(), chunk_idx);
}

Status LogIndex::OpenChunk(int64_t chunk_idx, scoped_refptr<IndexChunk>* chunk) {
  string path = GetChunkPath(chunk_idx);

  scoped_refptr<IndexChunk> new_chunk(new IndexChunk(path));
  RETURN_NOT_OK(new_chunk->Open());
  chunk->swap(new_chunk);
  return Status::OK();
}

Status LogIndex::GetChunkForIndex(int64_t log_index, bool create,
                                  scoped_refptr<IndexChunk>* chunk) {
  CHECK_GT(log_index, 0);
  int64_t chunk_idx = log_index / kEntriesPerIndexChunk;

  {
    lock_guard<simple_spinlock> l(&open_chunks_lock_);
    if (FindCopy(open_chunks_, chunk_idx, chunk)) {
      return Status::OK();
    }
  }

  if (!create) {
    return Status::NotFound("chunk not found");
  }

  RETURN_NOT_OK_PREPEND(OpenChunk(chunk_idx, chunk),
                        "Couldn't open index chunk");
  {
    lock_guard<simple_spinlock> l(&open_chunks_lock_);
    if (PREDICT_FALSE(ContainsKey(open_chunks_, chunk_idx))) {
      // Someone else opened the chunk in the meantime.
      // We'll just return that one.
      *chunk = FindOrDie(open_chunks_, chunk_idx);
      return Status::OK();
    }

    InsertOrDie(&open_chunks_, chunk_idx, *chunk);
  }

  return Status::OK();
}

Status LogIndex::AddEntry(const LogIndexEntry& entry) {
  scoped_refptr<IndexChunk> chunk;
  RETURN_NOT_OK(GetChunkForIndex(entry.op_id.index(),
                                 true /* create if not found */,
                                 &chunk));
  int index_in_chunk = entry.op_id.index() % kEntriesPerIndexChunk;

  PhysicalEntry phys;
  phys.term = entry.op_id.term();
  phys.segment_sequence_number = entry.segment_sequence_number;
  phys.offset_in_segment = entry.offset_in_segment;

  chunk->SetEntry(index_in_chunk, phys);
  VLOG(3) << "Added log index entry " << entry.ToString();

  return Status::OK();
}

Status LogIndex::GetEntry(int64_t index, LogIndexEntry* entry) {
  scoped_refptr<IndexChunk> chunk;
  RETURN_NOT_OK(GetChunkForIndex(index, false /* do not create */, &chunk));
  int index_in_chunk = index % kEntriesPerIndexChunk;
  PhysicalEntry phys;
  chunk->GetEntry(index_in_chunk, &phys);

  // We never write any real entries to offset 0, because there's a header
  // in each log segment. So, this indicates an entry that was never written.
  if (phys.offset_in_segment == 0) {
    return Status::NotFound("entry not found");
  }

  entry->op_id = consensus::MakeOpId(phys.term, index);
  entry->segment_sequence_number = phys.segment_sequence_number;
  entry->offset_in_segment = phys.offset_in_segment;

  return Status::OK();
}

void LogIndex::GC(int64_t min_index_to_retain) {
  int min_chunk_to_retain = min_index_to_retain / kEntriesPerIndexChunk;

  // Enumerate which chunks to delete.
  vector<int64_t> chunks_to_delete;
  {
    lock_guard<simple_spinlock> l(&open_chunks_lock_);
    for (auto it = open_chunks_.begin();
         it != open_chunks_.lower_bound(min_chunk_to_retain); ++it) {
      chunks_to_delete.push_back(it->first);
    }
  }

  // Outside of the lock, try to delete them (avoid holding the lock during IO).
  for (int64_t chunk_idx : chunks_to_delete) {
    string path = GetChunkPath(chunk_idx);
    int rc = unlink(path.c_str());
    if (rc != 0) {
      PLOG(WARNING) << "Unable to delete index chunk " << path;
      continue;
    }
    LOG(INFO) << "Deleted log index segment " << path;
    {
      lock_guard<simple_spinlock> l(&open_chunks_lock_);
      open_chunks_.erase(chunk_idx);
    }
  }
}

string LogIndexEntry::ToString() const {
  return Substitute("op_id=$0.$1 segment_sequence_number=$2 offset=$3",
                    op_id.term(), op_id.index(),
                    segment_sequence_number,
                    offset_in_segment);
}

} // namespace log
} // namespace kudu
