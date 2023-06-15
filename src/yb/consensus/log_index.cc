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

// The implementation of the Log Index.
//
// The log index is implemented by a set of on-disk files, each containing a fixed number
// (GetEntriesPerIndexChunk()) of fixed size entries. Each index chunk is numbered such that,
// for a given log index, we can determine which chunk contains its index entry by a
// simple division operation. Because the entries are fixed size, we can compute the
// index offset by a modulo.
//
// When the log is GCed, we remove any index chunks which are no longer needed, and
// unmap them.

#include "yb/consensus/log_index.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/mman.h>

#include <mutex>
#include <string>
#include <vector>

#include <boost/algorithm/string/predicate.hpp>

#include "yb/consensus/log_util.h"
#include "yb/consensus/log.messages.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/map-util.h"

#include "yb/util/atomic.h"
#include "yb/util/scope_exit.h"
#include "yb/util/env.h"
#include "yb/util/file_util.h"
#include "yb/util/flags.h"
#include "yb/util/locks.h"
#include "yb/util/logging.h"

DEFINE_UNKNOWN_int32(
    entries_per_index_block, 10000, "Number of entries per index block stored in WAL segment file");
TAG_FLAG(entries_per_index_block, advanced);

DEFINE_test_flag(
    int32, entries_per_log_index_chuck, 0,
    "DO NOT CHANGE IN PRODUCTION. If set to value greater than 0 - overrides "
    "GetEntriesPerIndexChunk() for testing purposes.");

using std::string;
using std::vector;

#define RETRY_ON_EINTR(ret, expr) do { \
  ret = expr; \
} while ((ret == -1) && (errno == EINTR));

namespace yb {
namespace log {

// The actual physical entry in the file.
// This mirrors LogIndexEntry but uses simple primitives only so we can
// read/write it via mmap.
// See LogIndexEntry for docs.
struct PhysicalEntry {
  int64_t term;
  int64_t segment_sequence_number;
  uint64_t offset_in_segment;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(term, segment_sequence_number, offset_in_segment);
  }
} PACKED;

int32_t GetEntriesPerIndexChunk() {
  // The number of index entries per index chunk.
  //
  // **** Note: This number cannot be changed after production!!!!! ***
  //
  // Why? Because, given a raft log index, the chunk (i.e. the specific index file) and index
  // of the entry within the file is determined via simple "/" and "%" calculations respectively
  // on this value. [Technically, if we did decide to change this number, we would have to
  // implement some logic to compute the number of entries in each file from its size. But
  // currently, that's not implemented.]
  //
  // On MacOS, ftruncate()'ing a file to its desired size before doing the mmap, immediately uses up
  // actual disk space, whereas on Linux, it appears to be lazy. Since MacOS is unlikely to be
  // used for production scenarios, to reduce disk space requirements and testing load (when
  // creating lots of tables (and therefore tablets)), we set this number to a lower value for
  // MacOS.
  //

#if defined(__APPLE__)
  static int32_t num_entries_per_index_chunk = 16 * 1024;
#else
  static int32_t num_entries_per_index_chunk = 1000000;
#endif

  static class ChunkSizeInitializer {
   public:
    ChunkSizeInitializer() {
      if (FLAGS_TEST_entries_per_log_index_chuck > 0) {
        num_entries_per_index_chunk = FLAGS_TEST_entries_per_log_index_chuck;
      }
    }
  } chunk_size_initializer;

  return num_entries_per_index_chunk;
}

int64_t GetChunkFileSize() {
  static int64_t chunk_file_size = GetEntriesPerIndexChunk() * sizeof(PhysicalEntry);
  return chunk_file_size;
}

int64_t TEST_GetEntriesPerIndexChunk() {
  return GetEntriesPerIndexChunk();
}

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
  uint8_t* GetPhysicalEntryPtr(int entry_index);
  void GetEntry(int entry_index, PhysicalEntry* ret);
  void SetEntry(int entry_index, const PhysicalEntry& entry);

  // Flush memory-mapped chunk to file.
  Status Flush();

  const std::string& path() const { return path_; }

 private:
  const string path_;
  int fd_;
  uint8_t* mapping_;
  // Lock to ensure that only one reader/writer is performing operation on mapping_.
  simple_spinlock entry_lock_;
};

namespace  {
Status CheckError(int rc, const char* operation) {
  if (PREDICT_FALSE(rc < 0)) {
    return STATUS(IOError, operation, Errno(errno));
  }
  return Status::OK();
}
} // anonymous namespace

LogIndex::IndexChunk::IndexChunk(std::string path)
    : path_(std::move(path)), fd_(-1), mapping_(nullptr) {
}

LogIndex::IndexChunk::~IndexChunk() {
  if (mapping_ != nullptr) {
    munmap(mapping_, GetChunkFileSize());
  }

  if (fd_ >= 0) {
    close(fd_);
  }
}

Status LogIndex::IndexChunk::Open() {
  RETRY_ON_EINTR(fd_, open(path_.c_str(), O_CLOEXEC | O_CREAT | O_RDWR, 0666));
  RETURN_NOT_OK(CheckError(fd_, "open"));

  int err;
  RETRY_ON_EINTR(err, ftruncate(fd_, GetChunkFileSize()));
  RETURN_NOT_OK(CheckError(fd_, "truncate"));

  mapping_ = static_cast<uint8_t*>(mmap(nullptr, GetChunkFileSize(), PROT_READ | PROT_WRITE,
                                        MAP_SHARED, fd_, 0));
  if (mapping_ == MAP_FAILED) {
    mapping_ = nullptr;
    return STATUS(IOError, "Unable to mmap()", Errno(errno));
  }

  return Status::OK();
}

uint8_t* LogIndex::IndexChunk::GetPhysicalEntryPtr(int entry_index) {
  DCHECK_GE(fd_, 0) << "Must Open() first";
  DCHECK_LT(entry_index, GetEntriesPerIndexChunk());

  return mapping_ + sizeof(PhysicalEntry) * entry_index;
}

void LogIndex::IndexChunk::GetEntry(int entry_index, PhysicalEntry* ret) {
  std::lock_guard l(entry_lock_);
  memcpy(ret, GetPhysicalEntryPtr(entry_index), sizeof(PhysicalEntry));
}

void LogIndex::IndexChunk::SetEntry(int entry_index, const PhysicalEntry& phys) {
  DVLOG_WITH_FUNC(4) << "path: " << path_ << " index_in_chunk: " << entry_index
                     << " entry: " << phys.ToString();
  std::lock_guard l(entry_lock_);
  memcpy(GetPhysicalEntryPtr(entry_index), &phys, sizeof(PhysicalEntry));
}

////////////////////////////////////////////////////////////
// LogIndex
////////////////////////////////////////////////////////////

LogIndex::LogIndex(const std::string& base_dir) : base_dir_(base_dir) {
}

LogIndex::~LogIndex() {
}

Result<scoped_refptr<LogIndex>> LogIndex::NewLogIndex(const std::string& base_dir) {
  auto result = scoped_refptr<LogIndex>(new LogIndex(base_dir));
  RETURN_NOT_OK(result->Init());
  return result;
}

namespace {

const char* kIndexChunkFileNamePrefix = "index.";

std::string GetChunkPath(const std::string& base_dir, const int64_t chunk_idx) {
  return StringPrintf("%s/%s%09" PRId64, base_dir.c_str(), kIndexChunkFileNamePrefix, chunk_idx);
}

bool IsIndexChunkFileName(std::string file_name) {
  return boost::starts_with(file_name, kIndexChunkFileNamePrefix);
}

} // namespace

Status LogIndex::Init() {
  DIR* const d = opendir(base_dir_.c_str());
  auto se = ScopeExit([d] {
    if (d) {
      closedir(d);
    }
  });
  if (d == nullptr) {
    return STATUS_FROM_ERRNO_SPECIAL_EIO_HANDLING(base_dir_, errno);
  }

  struct dirent* entry;
  // Delete existing index chunk files, because they are not durable (see description for LogIndex
  // class in log_index.h).
  errno = 0;
  while ((entry = readdir(d)) != nullptr) {
    const char* filename = entry->d_name;
    if ((strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0)) {
      continue;
    }
    if (!IsIndexChunkFileName(filename)) {
      continue;
    }
    auto filepath = Format("$0/$1", base_dir_, filename);
    int rc = unlink(filepath.c_str());
    if (rc != 0) {
      return STATUS_EC_FORMAT(IOError, Errno(errno), "Failed to delete index chunk $0", filepath);
    }
  }
  if (errno) {
    return STATUS_EC_FORMAT(IOError, Errno(errno), "Failed to read directory $0", base_dir_);
  }
  return Status::OK();
}

std::string LogIndex::GetChunkPath(const int64_t chunk_idx) {
  return log::GetChunkPath(base_dir_, chunk_idx);
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
  const int64_t chunk_idx = log_index / GetEntriesPerIndexChunk();
  DVLOG_WITH_FUNC(4) << "op_index: " << log_index << " chunk_idx: " << chunk_idx;

  {
    std::lock_guard l(open_chunks_lock_);
    if (FindCopy(open_chunks_, chunk_idx, chunk)) {
      DVLOG_WITH_FUNC(4) << "chunk_idx: " << chunk_idx << " path: " << (*chunk)->path();
      return Status::OK();
    }
  }

  if (!create) {
    return STATUS_FORMAT(NotFound, "chunk $0 for op index $1 not found", chunk_idx, log_index);
  }

  RETURN_NOT_OK_PREPEND(OpenChunk(chunk_idx, chunk),
                        "Couldn't open index chunk");
  DVLOG_WITH_FUNC(4) << "chunk_idx: " << chunk_idx << " path: " << (*chunk)->path();
  {
    std::lock_guard l(open_chunks_lock_);
    if (PREDICT_FALSE(ContainsKey(open_chunks_, chunk_idx))) {
      // Someone else opened the chunk in the meantime.
      // We'll just return that one.
      *chunk = FindOrDie(open_chunks_, chunk_idx);
      DVLOG_WITH_FUNC(4) << "chunk_idx: " << chunk_idx << "path: " << (*chunk)->path();
      return Status::OK();
    }

    InsertOrDie(&open_chunks_, chunk_idx, *chunk);
  }

  return Status::OK();
}

Status LogIndex::AddEntry(const LogIndexEntry& entry, const Overwrite overwrite) {
  // We are writing to each Raft log sequentially, so this function couldn't be invoked
  // from multiple threads concurrently.
  const auto op_index = entry.op_id.index;
  scoped_refptr<IndexChunk> chunk;
  RETURN_NOT_OK(GetChunkForIndex(op_index, /* create = */ true, &chunk));
  const int index_in_chunk = op_index % GetEntriesPerIndexChunk();

  PhysicalEntry phys;
  phys.term = entry.op_id.term;
  phys.segment_sequence_number = entry.segment_sequence_number;
  phys.offset_in_segment = entry.offset_in_segment;
  if (PREDICT_FALSE(!overwrite)) {
    // Check if destination entry at operation index inside chunk is empty (memory mapped file
    // content is zero-initialized), so we can load into it and won't overwrite existing index
    // entry.
    PhysicalEntry existing;
    chunk->GetEntry(index_in_chunk, &existing);
    if (existing.offset_in_segment != 0) {
      // Destination entry for the op_idex already exists - don't overwrite.
      return Status::OK();
    }
  }
  chunk->SetEntry(index_in_chunk, phys);

  DVLOG(3) << "Added log index entry " << entry.ToString();

  return Status::OK();
}

Status LogIndex::GetEntry(int64_t index, LogIndexEntry* entry) {
  const auto max_gced_op_index = max_gced_op_index_.load(std::memory_order_acquire);
  DVLOG_WITH_FUNC(4) << "index: " << index << " max_gced_op_index: " << max_gced_op_index;
  if (index <= max_gced_op_index) {
    return STATUS_FORMAT(
        NotFound, "op index $0 has been already GCed from log index cache, max_gced_op_index: $1",
        index, max_gced_op_index);
  }
  scoped_refptr<IndexChunk> chunk;
  auto s = GetChunkForIndex(index, false /* do not create */, &chunk);
  if (s.IsNotFound()) {
    // Return Incomplete error, so upper layer can lazily load log index blocks from WAL segments
    // into LogIndex if they are not yet loaded.
    s = s.CloneAndReplaceCode(Status::kIncomplete);
  }
  RETURN_NOT_OK(s);
  int index_in_chunk = index % GetEntriesPerIndexChunk();
  PhysicalEntry phys;
  chunk->GetEntry(index_in_chunk, &phys);

  // We never write any real entries to offset 0, because there's a header
  // in each log segment. So, this indicates an entry that was never written.
  // We return Incomplete error, so upper layer can lazily load log index blocks from WAL segments
  // into LogIndex if they are not yet loaded.
  if (phys.offset_in_segment == 0) {
    return STATUS_FORMAT(Incomplete, "Log index cache entry for op index $0 not found", index);
  }

  entry->op_id = yb::OpId(phys.term, index);
  entry->segment_sequence_number = phys.segment_sequence_number;
  entry->offset_in_segment = phys.offset_in_segment;

  return Status::OK();
}

void LogIndex::GC(int64_t min_index_to_retain) {
  UpdateAtomicMax(&max_gced_op_index_, min_index_to_retain - 1);
  int64_t min_chunk_to_retain = min_index_to_retain / GetEntriesPerIndexChunk();

  // Enumerate which chunks to delete.
  vector<int64_t> chunks_to_delete;
  {
    std::lock_guard l(open_chunks_lock_);
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
      std::lock_guard l(open_chunks_lock_);
      open_chunks_.erase(chunk_idx);
    }
  }
}

Result<LogIndexBlock> LogIndex::GetIndexBlock(
    const int64_t start_index, const int64_t max_included_index) {
  scoped_refptr<IndexChunk> chunk;
  RETURN_NOT_OK(GetChunkForIndex(start_index, /* create = */ false, &chunk));

  const int32_t start_index_in_chunk = start_index % GetEntriesPerIndexChunk();
  auto* p = chunk->GetPhysicalEntryPtr(start_index_in_chunk);
  if (start_index / GetEntriesPerIndexChunk() == max_included_index / GetEntriesPerIndexChunk()) {
    // This is the last chunk.
    // (max_included_index - start_index + 1) is guaranteed to be <= GetEntriesPerIndexChunk() that
    // has int32_t type, so we can cast result to int32_t.
    auto num_entries = narrow_cast<int32_t>(max_included_index - start_index + 1);
    bool is_last_block;
    if (num_entries > FLAGS_entries_per_index_block) {
      num_entries = FLAGS_entries_per_index_block;
      is_last_block = false;
    } else {
      is_last_block = true;
    }
    return LogIndexBlock {
        .data = Slice(p, num_entries * sizeof(PhysicalEntry)),
        .start_op_index = start_index,
        .num_entries = num_entries,
        .is_last_block = is_last_block,
    };
  }

  // max_included_index is after this chunk operation index range.
  const auto num_entries =
      std::min(GetEntriesPerIndexChunk() - start_index_in_chunk, FLAGS_entries_per_index_block);
  return LogIndexBlock {
      .data = Slice(p, num_entries * sizeof(PhysicalEntry)),
      .start_op_index = start_index,
      .num_entries = num_entries,
      .is_last_block = false
  };
}

int64_t LogIndex::GetMinIndexedSegmentNumber() const {
  return min_indexed_segment_number_.load(std::memory_order_acquire);
}

void LogIndex::SetMinIndexedSegmentNumber(const int64_t segment_number) {
  min_indexed_segment_number_.store(segment_number, std::memory_order_release);
}

Result<bool> LogIndex::LoadFromSegment(ReadableLogSegment* segment) {
  const auto segment_number = segment->header().sequence_number();
  const auto min_indexed_segment_number = GetMinIndexedSegmentNumber();
  // kNoIndexForFullWalSegment means this object doesn't have index for any WAL
  // segment.
  if (min_indexed_segment_number != kNoIndexForFullWalSegment &&
      segment_number >= min_indexed_segment_number) {
    // Already loaded.
    return true;
  }

  const auto has_footer = segment->HasFooter();
  if (has_footer && segment->footer().num_entries() == 0) {
    return true;
  }

  VLOG_WITH_FUNC(1) << "path: " << segment->path() << " footer: "
                    << (has_footer ? segment->footer().ShortDebugString() : "---");

  const auto first_op_index = VERIFY_RESULT(DoLoadFromSegment(segment));

  // Here we rely on initial value (kNoIndexForFullWalSegment) to be larger than any other possible
  // value stored inside min_indexed_segment_number_.
  UpdateAtomicMin(&min_indexed_segment_number_, segment_number);

  if (first_op_index <= max_gced_op_index_.load(std::memory_order_acquire)) {
    // GC removed at least beginning of Raft operations we have just loaded index for.
    return false;
  }
  // All operations we have just loaded index for are after GCed part of log.
  // If GC happens after returning from this function, caller might try to load previous segment
  // using this function, will get true from next invocation and will stop loading. This is
  // harmless.
  return true;
}

Result<int64_t> LogIndex::RebuildFromSegmentEntries(ReadableLogSegment* segment) {
  auto read_entries = segment->ReadEntries();
  RETURN_NOT_OK(read_entries.status);
  LogIndexEntry index_entry;
  index_entry.segment_sequence_number = segment->header().sequence_number();
  int64_t first_op_index = std::numeric_limits<int64_t>::max();
  LOG(INFO) << "Start building log index from " << segment->path() << " having "
            << read_entries.entries.size() << " entries";
  // Rebuild in reverse order so we have latest info in case of op_index overwrite due to term
  // change.
  for (size_t i = read_entries.entries.size(); i > 0;) {
    --i;
    const auto& entry = read_entries.entries[i];
    VLOG_WITH_FUNC(4) << "Entry: " << entry->ShortDebugString();
    if (!entry->has_replicate()) {
      continue;
    }
    const auto& meta = read_entries.entry_metadata[i];
    index_entry.op_id = yb::OpId::FromPB(entry->replicate().id());
    index_entry.offset_in_segment = meta.offset;
    RETURN_NOT_OK(AddEntry(index_entry, Overwrite::kFalse));
    first_op_index = std::min(first_op_index, index_entry.op_id.index);
  }
  if (first_op_index == std::numeric_limits<int64_t>::max()) {
    first_op_index = 1;
  }
  LOG(INFO) << "Rebuilt log index starting at op_index " << first_op_index
            << " from segment: " << segment->path();
  return first_op_index;
}

Result<int64_t> LogIndex::DoLoadFromSegment(ReadableLogSegment* segment) {
  if (!segment->HasFooter() || segment->footer().index_start_offset() <= 0) {
    return RebuildFromSegmentEntries(segment);
  }

  uint64_t offset = segment->footer().index_start_offset();

  faststring buf;
  int64_t first_op_index = -1;
  int64_t next_block_op_index = -1;
  while (true) {
    auto index_block = VERIFY_RESULT(segment->ReadIndexBlock(&offset, &buf));
    if (first_op_index < 0) {
      first_op_index = index_block.start_op_index;
    }
    if (next_block_op_index >= 0 && next_block_op_index != index_block.start_op_index) {
      return STATUS_FORMAT(
          Corruption, "Expected index block start op index: $0, got: $1", next_block_op_index,
          index_block.start_op_index);
    }
    RETURN_NOT_OK(LoadIndexBlock(index_block));
    next_block_op_index = index_block.start_op_index + index_block.num_entries;
    if (index_block.is_last_block) {
      break;
    }
  }

  VLOG_WITH_FUNC(2) << "Loaded first_op_index: " << first_op_index
                    << " next_op_index: " << next_block_op_index;
  return first_op_index;
}

Status LogIndex::LoadIndexBlock(const LogIndexBlock& index_block) {
  scoped_refptr<IndexChunk> chunk;
  RSTATUS_DCHECK_EQ(
      index_block.num_entries, index_block.data.size() / sizeof(PhysicalEntry), InternalError,
      Format("Num_entries mismatch, num_entries: $0, block size: $1, sizeof(PhysicalEntry): $2",
             index_block.num_entries, index_block.data.size(), sizeof(PhysicalEntry)));

  VLOG_WITH_FUNC(3) << "Loading index block, start operation index: " << index_block.start_op_index
                    << " last operation index: "
                    << index_block.start_op_index + index_block.num_entries - 1;

  // We load index for segments from newest to oldest and don't overwrite already existing index
  // entries to have relevant index entries for rewritten Raft operation indexes.
  auto op_index = index_block.start_op_index;
  auto num_entries_left = index_block.num_entries;
  const auto* src = index_block.data.data();

  while (num_entries_left > 0) {
    RETURN_NOT_OK(GetChunkForIndex(op_index, /* create = */ true, &chunk));
    const int32_t op_index_in_chunk = op_index % GetEntriesPerIndexChunk();
    auto num_entries_left_in_chunk =
        std::min<int64_t>(num_entries_left, GetEntriesPerIndexChunk() - op_index_in_chunk);
    auto* dst = chunk->GetPhysicalEntryPtr(op_index_in_chunk);
    while (num_entries_left_in_chunk > 0) {
      // First check if destination entry for operation index inside chunk (at dst) is empty (memory
      // mapped file content is zero-initialized), so we can load into it and won't overwrite
      // existing index entry.
      PhysicalEntry entry;
      memcpy(&entry, dst, sizeof(PhysicalEntry));
      if (entry.offset_in_segment == 0) {
        // Entry at this operation index doesn't yet exist - ok to load.
        memcpy(dst, src, sizeof(PhysicalEntry));
        if (VLOG_IS_ON(4)) {
          memcpy(&entry, dst, sizeof(PhysicalEntry));
          VLOG_WITH_FUNC(4) << "Loaded for op_index: " << op_index << " entry:" << entry.ToString();
        }
      }
      // We load segments from newest to oldest, but within the segment it is ok to load from
      // oldest to newest, because log index blocks are written after all operation overwrites
      // within the segment are already resolved. So we don't need to handle potential Raft entries
      // overwrites within the same segment, index blocks within the segment already contains
      // latest data at the moment of closing WAL segment.
      src += sizeof(PhysicalEntry);
      dst += sizeof(PhysicalEntry);
      ++op_index;
      --num_entries_left_in_chunk;
      --num_entries_left;
    }
  }
  return Status::OK();
}

string LogIndexEntry::ToString() const {
  return YB_STRUCT_TO_STRING(op_id, segment_sequence_number, offset_in_segment);
}

} // namespace log
} // namespace yb
