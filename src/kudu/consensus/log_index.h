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
#ifndef KUDU_CONSENSUS_LOG_INDEX_H
#define KUDU_CONSENSUS_LOG_INDEX_H

#include <string>
#include <map>

#include "kudu/consensus/consensus.pb.h"
#include "kudu/gutil/macros.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/util/locks.h"
#include "kudu/util/status.h"

namespace kudu {
namespace log {

// An entry in the index.
struct LogIndexEntry {
  consensus::OpId op_id;

  // The sequence number of the log segment which contains this entry.
  int64_t segment_sequence_number;

  // The offset within that log segment for the batch which contains this
  // entry. Note that the offset points to an entire batch which may contain
  // more than one replicate.
  int64_t offset_in_segment;

  std::string ToString() const;
};

// An on-disk structure which indexes from OpId index to the specific position in the WAL
// which contains the latest ReplicateMsg for that index.
//
// This structure is on-disk but *not durable*. We use mmap()ed IO to write it out, and
// never sync it to disk. Its only purpose is to allow random-reading earlier entries from
// the log to serve to Raft followers.
//
// This class is thread-safe, but doesn't provide a memory barrier between writers and
// readers. In other words, if a reader is expected to see an index entry written by a
// writer, there should be some other synchronization between them to ensure visibility.
//
// See .cc file for implementation notes.
class LogIndex : public RefCountedThreadSafe<LogIndex> {
 public:
  explicit LogIndex(std::string base_dir);

  // Record an index entry in the index.
  Status AddEntry(const LogIndexEntry& entry);

  // Retrieve an existing entry from the index.
  // Returns NotFound() if the given log entry was never written.
  Status GetEntry(int64_t index, LogIndexEntry* entry);

  // Indicate that we no longer need to retain information about indexes lower than the
  // given index. Note that the implementation is conservative and _may_ choose to retain
  // earlier entries.
  void GC(int64_t min_index_to_retain);

 private:
  friend class RefCountedThreadSafe<LogIndex>;
  ~LogIndex();

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

  // The base directory where index files are located.
  const std::string base_dir_;

  simple_spinlock open_chunks_lock_;

  // Map from chunk index to IndexChunk. The chunk index is the log index modulo
  // the number of entries per chunk (see docs in log_index.cc).
  // Protected by open_chunks_lock_
  typedef std::map<int64_t, scoped_refptr<IndexChunk> > ChunkMap;
  ChunkMap open_chunks_;

  DISALLOW_COPY_AND_ASSIGN(LogIndex);
};

} // namespace log
} // namespace kudu
#endif /* KUDU_CONSENSUS_LOG_INDEX_H */
