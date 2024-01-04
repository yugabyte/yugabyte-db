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
#pragma once

#include <map>
#include <string>

#include "yb/gutil/macros.h"
#include "yb/gutil/ref_counted.h"

#include "yb/util/result.h"
#include "yb/util/status_fwd.h"
#include "yb/util/locks.h"
#include "yb/util/opid.h"

namespace yb {

class Env;

namespace log {

YB_STRONGLY_TYPED_BOOL(Overwrite);

// An entry in the index.
struct LogIndexEntry {
  yb::OpId op_id;

  // The sequence number of the log segment which contains this entry.
  int64_t segment_sequence_number;

  // The offset within that log segment for the batch which contains this
  // entry. Note that the offset points to an entire batch which may contain
  // more than one replicate.
  int64_t offset_in_segment;

  std::string ToString() const;
};

struct LogIndexBlock {
  // Points to log index raw data for this block. Stored as a sequence of PhysicalEntry structure.
  // Owned by LogIndex cache or external buffer.
  Slice data;
  int64_t start_op_index;
  int32_t num_entries;
  bool is_last_block;
};

class ReadableLogSegment;

// An on-disk structure which indexes from OpId index to the specific position in the WAL
// which contains the latest ReplicateMsg for that index.
//
// This structure is on-disk but *not durable*. We use mmap()ed IO to write it out, and
// never sync it to disk. Its only purpose is to allow random-reading earlier entries from
// the log to serve to Raft followers.
// Since this class uses direct mmap()ed IO, it doesn't need Env and uses file manipulation OS
// functions directly.
//
// This class is thread-safe, but doesn't provide a memory barrier between writers and
// readers. In other words, if a reader is expected to see an index entry written by a
// writer, there should be some other synchronization between them to ensure visibility.
//
// See .cc file for implementation notes.
class LogIndex : public RefCountedThreadSafe<LogIndex> {
 public:
  static Result<scoped_refptr<LogIndex>> NewLogIndex(const std::string& base_dir);

  // Record an index entry in the index.
  Status AddEntry(const LogIndexEntry& entry, Overwrite overwrite = Overwrite::kTrue);

  // Retrieve an existing entry from the index.
  // Returns NotFound() if the given log entry was never written.
  Status GetEntry(int64_t index, LogIndexEntry* entry);

  // Returns index block covering operations from start_index up to and including
  // max_included_index, but no more than FLAGS_entries_per_index_block.
  Result<LogIndexBlock> GetIndexBlock(int64_t start_index, int64_t max_included_index);

  // Loads log index from the segment file.
  // In can happen that concurrent GC will remove at least the beginning of Raft operations
  // belonging to this segment.
  // In this case we return false to indicate that we don't need to load earlier segments further.
  // Otherwise we return true.
  Result<bool> LazyLoadOneSegment(ReadableLogSegment* segment);

  // Loads log index from the given segment file, save index of the first op in this segment
  // to first_op_index.
  // Return false to indicate that the segment has already been loaded and LoadFromSegment just
  // skipped it. Otherwise, return true.
  Result<bool> LoadFromSegment(ReadableLogSegment* segment, int64_t* first_op_index);

  // Indicate that we no longer need to retain information about indexes lower than the
  // given index. Note that the implementation is conservative and _may_ choose to retain
  // earlier entries.
  void GC(int64_t min_index_to_retain);

  // Flushes log index to disk.
  Status Flush();

  // Or kNoIndexForFullWalSegment if this object doesn't have full index for any WAL segment.
  int64_t GetMinIndexedSegmentNumber() const;

  void SetMinIndexedSegmentNumber(int64_t segment_number);

  // We rely on min_indexed_segment_number_ initial value (kNoIndexForFullWalSegment) to be larger
  // than any other possible value stored inside min_indexed_segment_number_.
  static constexpr int64_t kNoIndexForFullWalSegment =
      std::numeric_limits<int64_t>::max();

 private:
  friend class RefCountedThreadSafe<LogIndex>;

  explicit LogIndex(const std::string& base_dir);
  ~LogIndex();

  Status Init();

  class IndexChunk;

  // Open the on-disk chunk with the given index.
  // Note: 'chunk_idx' is the index of the index chunk, not the index of a log _entry_.
  Status OpenChunk(int64_t chunk_idx, scoped_refptr<IndexChunk>* chunk);

  // Return the index chunk which contains the given log index.
  // If 'create' is true, creates it on-demand. If 'create' is false, and
  // the index chunk does not exist, returns NotFound.
  Status GetChunkForIndex(int64_t log_index, bool create,
                          scoped_refptr<IndexChunk>* chunk);

  // Return the path of the given index chunk.
  std::string GetChunkPath(int64_t chunk_idx);

  // Loads index block into index. Doesn't overwrite already loaded entries.
  Status LoadIndexBlock(const LogIndexBlock& index_block);

  // Loads index from segment index blocks. Falls back to RebuildFromSegmentEntries if there are no
  // index blocks inside WAL segment.
  // Returns first operation index.
  Result<int64_t> DoLoadFromSegment(ReadableLogSegment* segment);

  // Updates index by scanning WAL segment entries.
  // Returns first (minimal) operation index.
  Result<int64_t> RebuildFromSegmentEntries(ReadableLogSegment* segment);

  // The base directory where index files are located.
  const std::string base_dir_;

  simple_spinlock open_chunks_lock_;

  // Map from chunk index to IndexChunk. The chunk index is the log index modulo
  // the number of entries per chunk (see docs in log_index.cc).
  // Protected by open_chunks_lock_
  typedef std::map<int64_t, scoped_refptr<IndexChunk> > ChunkMap;
  ChunkMap open_chunks_;

  // Maximum garbage collected operation index or negative number if no GC happened yet.
  // Initially should be set to arbitrary negative number.
  std::atomic<int64_t> max_gced_op_index_{-1};

  // Minimum WAL segment number for which we have full index inside this LogIndex instance
  // (backed by memory-mapped files).
  std::atomic<int64_t> min_indexed_segment_number_{kNoIndexForFullWalSegment};

  DISALLOW_COPY_AND_ASSIGN(LogIndex);
};

int64_t TEST_GetEntriesPerIndexChunk();

} // namespace log
} // namespace yb
