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

#include <iosfwd>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest_prod.h>

#include "yb/common/opid.h"

#include "yb/consensus/consensus_fwd.h"
#include "yb/consensus/log_fwd.h"
#include "yb/consensus/log.fwd.h"
#include "yb/consensus/log.pb.h"

#include "yb/gutil/macros.h"
#include "yb/gutil/ref_counted.h"

#include "yb/util/compare_util.h"
#include "yb/util/env.h"
#include "yb/util/monotime.h"
#include "yb/util/restart_safe_clock.h"
#include "yb/util/status.h"
#include "yb/util/tostring.h"

// Used by other classes, now part of the API.
DECLARE_bool(durable_wal_write);
DECLARE_bool(require_durable_wal_write);
DECLARE_string(fs_wal_dirs);
DECLARE_string(fs_data_dirs);

namespace yb {
namespace log {

// Suffix for temporary files
extern const char kTmpSuffix[];

// Each log entry is prefixed by its length (4 bytes), CRC (4 bytes),
// and checksum of the other two fields (see EntryHeader struct below).
extern const size_t kEntryHeaderSize;

extern const int kLogMajorVersion;
extern const int kLogMinorVersion;

// Options for the Write Ahead Log. The LogOptions constructor initializes default field values
// based on flags. See log_util.cc for details.
struct LogOptions {

  // The size of a Log segment.
  // Logs will roll over upon reaching this size.
  size_t segment_size_bytes;

  size_t initial_segment_size_bytes;

  // Whether to call fsync on every call to Append().
  bool durable_wal_write;

  // If non-zero, call fsync on a call to Append() every interval of time.
  MonoDelta interval_durable_wal_write;

  // If non-zero, call fsync on a call to Append() if more than given amount of data to sync.
  int32_t bytes_durable_wal_write_mb;

  // Whether to fallocate segments before writing to them.
  bool preallocate_segments;

  // Whether the allocation should happen asynchronously.
  bool async_preallocate_segments;

  uint32_t retention_secs = 0;

  // Env for log file operations.
  Env* env;

  std::string peer_uuid;

  int64_t initial_active_segment_sequence_number = 0;

  LogOptions();
};

struct LogEntryMetadata {
  RestartSafeCoarseTimePoint entry_time;
  int64_t offset;
  int64_t active_segment_sequence_number;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(entry_time, offset, active_segment_sequence_number);
  }

  friend bool operator==(const LogEntryMetadata& lhs, const LogEntryMetadata& rhs) {
    return YB_STRUCT_EQUALS(entry_time, offset, active_segment_sequence_number);
  }
};

// A sequence of segments, ordered by increasing sequence number.
typedef std::vector<std::shared_ptr<LWLogEntryPB>> LogEntries;

struct ReadEntriesResult {
  // Read entries
  LogEntries entries;

  // Time, offset in WAL, and sequence number of respective entry
  std::vector<LogEntryMetadata> entry_metadata;

  // Where we finished reading
  int64_t end_offset;

  OpId committed_op_id;

  // Failure status
  Status status;
};

struct FirstEntryMetadata {
  OpId op_id;
  RestartSafeCoarseTimePoint entry_time;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(op_id, entry_time);
  }
};

YB_DEFINE_ENUM(EntriesToRead, (kAll)(kReplicate));

// A segment of the log can either be a ReadableLogSegment (for replay and
// consensus catch-up) or a WritableLogSegment (where the Log actually stores
// state). LogSegments have a maximum size defined in LogOptions (set from the
// log_segment_size_mb flag, which defaults to 64). Upon reaching this size
// segments are rolled over and the Log continues in a new segment.

// A readable log segment for recovery and follower catch-up.
class ReadableLogSegment : public RefCountedThreadSafe<ReadableLogSegment> {
 public:
  // Factory method to construct a ReadableLogSegment from a file on the FS.
  static Result<scoped_refptr<ReadableLogSegment>> Open(Env* env, const std::string& path);

  // Build a readable segment to read entries from the provided path.
  ReadableLogSegment(std::string path,
                     std::shared_ptr<RandomAccessFile> readable_file);

  // Initialize the ReadableLogSegment.
  // This initializer provides methods for avoiding disk IO when creating a
  // ReadableLogSegment for the current WritableLogSegment, i.e. for reading
  // the log entries in the same segment that is currently being written to.
  Status Init(const LogSegmentHeaderPB& header,
              int64_t first_entry_offset);

  // Initialize the ReadableLogSegment.
  // This initializer provides methods for avoiding disk IO when creating a
  // ReadableLogSegment from a WritableLogSegment (i.e. for log rolling).
  Status Init(const LogSegmentHeaderPB& header,
              const LogSegmentFooterPB& footer,
              int64_t first_entry_offset);

  // Initialize the ReadableLogSegment.
  // This initializer will parse the log segment header and footer.
  // Returns false if it is empty segment, that could be ignored.
  Result<bool> Init();

  // Reads all entries of the provided segment.
  //
  // If start_offset_to_read is specified, should use it as the starting point to read.
  //
  // If the log is corrupted (i.e. the returned 'Status' is 'Corruption') all
  // the log entries read up to the corrupted one are returned in the 'entries'
  // vector.
  //
  // All gathered information is returned in result.
  // In case of failure status field of result is not ok.
  //
  // Will stop after reading max_entries_to_read entries.
  ReadEntriesResult ReadEntries(
      int64_t max_entries_to_read = std::numeric_limits<int64_t>::max(),
      EntriesToRead entries_to_read = EntriesToRead::kAll,
      std::optional<int64_t> start_offset_to_read = std::nullopt);

  // Reads the metadata of the first entry in the segment
  Result<FirstEntryMetadata> ReadFirstEntryMetadata();

  // Returns op id of the first replicate entry.
  // - NotFound error if no replicate entries found.
  // - Other errors if failed to read.
  Result<OpId> ReadFirstReplicateEntryOpId();

  // Rebuilds this segment's footer by scanning its entries.
  // This is an expensive operation as it reads and parses the whole segment
  // so it should be only used in the case of a crash, where the footer is
  // missing because we didn't have the time to write it out.
  Status RebuildFooterByScanning();

  Status RebuildFooterByScanning(const ReadEntriesResult& read_entries);

  // Restore footer_builder and log_index for log's active_segment.Similar to
  // RebuildFooterByScanning(), this is also only used in the case of crash.
  // The difference is, instead of rebuilding its footer and closing this
  // uncompleted segment, we restore footer_builder_ and log_index_'s progress,
  // and then reuse this segment as writable active_segment.
  Status RestoreFooterBuilderAndLogIndex(LogSegmentFooterPB* footer_builder,
                                         LogIndex* log_index,
                                         const ReadEntriesResult& read_entries);

  // Copies log segment up to up_to_op_id into new segment at dest_path.
  Status CopyTo(
      Env* env, const WritableFileOptions& writable_file_options, const std::string& dest_path,
      const OpId& up_to_op_id);

  // Reads index block from file at specified offset into tmp_buf.
  Result<LogIndexBlock> ReadIndexBlock(uint64_t* offset, faststring* tmp_buf);

  bool IsInitialized() const {
    return is_initialized_;
  }

  bool HasLogIndexInFooter() const {
    return HasFooter() && footer().index_start_offset() > 0;
  }

  // Returns the parent directory where log segments are stored.
  const std::string &path() const {
    return path_;
  }

  const LogSegmentHeaderPB& header() const;

  // Indicates whether this segment has a footer.
  //
  // Segments that were properly closed, e.g. because they were rolled over,
  // will have properly written footers. On the other hand if there was a
  // crash and the segment was not closed properly the footer will be missing.
  // In this case calling ReadEntries() will rebuild the footer.
  bool HasFooter() const {
    return footer_.IsInitialized();
  }

  // Returns this log segment's footer.
  //
  // If HasFooter() returns false this cannot be called.
  const LogSegmentFooterPB& footer() const {
    DCHECK(IsInitialized());
    CHECK(HasFooter());
    return footer_;
  }

  std::shared_ptr<RandomAccessFile> readable_file() const {
    return readable_file_;
  }

  std::shared_ptr<RandomAccessFile> readable_file_checkpoint() const {
    return readable_file_checkpoint_;
  }

  int64_t file_size() const {
    return file_size_.load(std::memory_order_acquire);
  }

  int64_t first_entry_offset() const {
    return first_entry_offset_;
  }

  int64_t get_encryption_header_size() const {
    return readable_file_->GetEncryptionHeaderSize();
  }

  int64_t readable_to_offset() const {
    return readable_to_offset_.load(std::memory_order_acquire);
  }

  bool footer_was_rebuilt() const {
    return footer_was_rebuilt_;
  }

 private:
  friend class RefCountedThreadSafe<ReadableLogSegment>;
  friend class LogReader;
  FRIEND_TEST(LogTest, TestWriteAndReadToAndFromInProgressSegment);

  struct EntryHeader {
    // The length of the batch data.
    uint32_t msg_length;

    // The CRC32C of the batch data.
    uint32_t msg_crc;

    // The CRC32C of this EntryHeader.
    uint32_t header_crc;
  };

  ~ReadableLogSegment() {}

  // Helper functions called by Init().

  Status ReadFileSize();

  Result<bool> ReadHeader();

  Status ReadHeaderMagicAndHeaderLength(uint32_t *len);

  Status ParseHeaderMagicAndHeaderLength(const Slice &data, uint32_t *parsed_len);

  Status ReadFooter();

  Status ReadFooterMagicAndFooterLength(uint32_t *len);

  Status ParseFooterMagicAndFooterLength(const Slice &data, uint32_t *parsed_len);

  // Starting at 'offset', read the rest of the log file, looking for any
  // valid log entry headers.
  //
  // Returns true/false based on the valid entries found. In case of any IO error,
  // a bad Status code is returned.
  Result<bool> ScanForValidEntryHeaders(int64_t offset);

  // Format a nice error message to report on a corruption in a log file.
  Status MakeCorruptionStatus(
      size_t batch_number, int64_t batch_offset, std::vector<int64_t>* recent_offsets,
      const LogEntries& entries, const Status& status) const;

  Result<std::shared_ptr<LWLogEntryBatchPB>> ReadEntryHeaderAndBatch(int64_t* offset);

  // Reads a log entry header from the segment.
  // Also increments the passed offset* by the length of the entry.
  Status ReadEntryHeader(int64_t *offset, EntryHeader* header);

  // Decode a log entry header from the given slice, which must be kEntryHeaderSize
  // bytes long. Returns true if successful, false if corrupt.
  //
  // NOTE: this is performance-critical since it is used by ScanForValidEntryHeaders
  // and thus returns bool instead of Status.
  Status DecodeEntryHeader(const Slice& data, EntryHeader* header);

  // Reads a log entry batch from the provided readable segment, which gets decoded
  // into 'entry_batch' and increments 'offset' by the batch's length.
  Result<std::shared_ptr<LWLogEntryBatchPB>> ReadEntryBatch(
      int64_t *offset, const EntryHeader& header);

  void UpdateReadableToOffset(int64_t readable_to_offset);

  int64_t ReadEntriesUpTo();

  const std::string path_;

  // The size of the readable file.
  // This is set by Init(). In the case of a log being written to, this may be increased by
  // UpdateReadableToOffset().
  std::atomic<int64_t> file_size_;

  // The offset up to which we can read the file.
  // Contains full size of the file, if the segment is closed and has a footer, or the offset where
  // the last written, non corrupt entry ends.
  // This is atomic because the Log thread might be updating the segment's readable
  // offset while an async reader is reading the segment's entries.
  std::atomic<int64_t> readable_to_offset_;

  // a readable file for a log segment (used on replay)
  const std::shared_ptr<RandomAccessFile> readable_file_;

  std::shared_ptr<RandomAccessFile> readable_file_checkpoint_;

  bool is_initialized_;

  LogSegmentHeaderPB header_;

  LogSegmentFooterPB footer_;

  // True if the footer was rebuilt, rather than actually found on disk.
  bool footer_was_rebuilt_;

  // the offset of the first entry in the log.
  int64_t first_entry_offset_;

  DISALLOW_COPY_AND_ASSIGN(ReadableLogSegment);
};

// A writable log segment where state data is stored. The class is not thread safe.
// It is still okay to call ::Sync and ::WriteEntryBatch from two different threads
// as long as write/append/truncate etc are being done by the same thread.
// ::Sync ends up calling 'fsync' system call and resets 'pending_sync_' prior to that.
// ::WriteEntryBatch results in a call to 'writev' and is followed by setting 'pending_sync_'.
// Both these system calls are atomic and hence doing so is safe.
class WritableLogSegment {
 public:
  WritableLogSegment(std::string path,
                     std::shared_ptr<WritableFile> writable_file);

  // This initializer method avoids writing header to disk when creating a WritableLogSegment
  // from a ReadableLogSegment.
  Status ReuseHeader(const LogSegmentHeaderPB& new_header,
                            int64_t first_entry_offset);

  // Opens the segment by writing the header.
  Status WriteHeader(const LogSegmentHeaderPB& new_header);

  // Closes the segment by writing the index, the footer, and then actually closing the
  // underlying WritableFile.
  Status WriteIndexWithFooterAndClose(LogIndex* log_index, LogSegmentFooterPB* footer);

  bool IsClosed() {
    return IsHeaderWritten() && IsFooterWritten();
  }

  int64_t Size() const {
    return writable_file_->Size();
  }

  // Appends the provided batch of data, including a header
  // and checksum.
  // Makes sure that the log segment has not been closed.
  Status WriteEntryBatch(const Slice& entry_batch_data);

  // Makes sure the I/O buffers in the underlying writable file are flushed.
  Status Sync();

  // Returns true if the segment header has already been written to disk.
  bool IsHeaderWritten() const {
    return is_header_written_;
  }

  const LogSegmentHeaderPB& header() const {
    DCHECK(IsHeaderWritten());
    return header_;
  }

  bool IsFooterWritten() const {
    return is_footer_written_;
  }

  const LogSegmentFooterPB& footer() const {
    DCHECK(IsFooterWritten());
    return footer_;
  }

  // Returns the parent directory where log segments are stored.
  const std::string &path() const {
    return path_;
  }

  void set_path(const std::string& path) {
    path_ = path;
  }

  int64_t first_entry_offset() const {
    return first_entry_offset_;
  }

  int64_t written_offset() const {
    return writable_file_->Size();
  }

  // Write header without data. This help us to simulate crash
  // in the middle of writing a entry
  Status TEST_WriteCorruptedEntryBatchAndSync();

 private:

  const std::shared_ptr<WritableFile>& writable_file() const {
    return writable_file_;
  }

  // Writes index stored inside log_index within specified range into the WAL segment file.
  Status WriteIndex(LogIndex* log_index, int64_t start_index, int64_t end_index_inclusive);

  Status WriteIndexBlock(const LogIndexBlock& index_block);

  // The path to the log file.
  std::string path_;

  // The writable file to which this LogSegment will be written.
  const std::shared_ptr<WritableFile> writable_file_;

  bool is_header_written_;

  bool is_footer_written_;

  LogSegmentHeaderPB header_;

  LogSegmentFooterPB footer_;

  // the offset of the first entry in the log
  int64_t first_entry_offset_;

  faststring index_block_header_buffer_;

  DISALLOW_COPY_AND_ASSIGN(WritableLogSegment);
};

using consensus::ReplicateMsgs;

// Sets 'batch' to a newly created batch that contains the pre-allocated
// ReplicateMsgs in 'msgs'.
// We use C-style passing here to avoid having to allocate a vector
// in some hot paths.
std::shared_ptr<LWLogEntryBatchPB> CreateBatchFromAllocatedOperations(const ReplicateMsgs& msgs);

// Checks if 'fname' is a correctly formatted name of log segment file.
bool IsLogFileName(const std::string& fname);

Status CheckPathsAreODirectWritable(const std::vector<std::string>& paths);
Status CheckRelevantPathsAreODirectWritable();

// Modify durable wal write flag depending on the value of FLAGS_require_durable_wal_write.
Status ModifyDurableWriteFlagIfNotODirect();

void UpdateSegmentFooterIndexes(
    const consensus::LWReplicateMsg& replicate, LogSegmentFooterPB* footer);

}  // namespace log
}  // namespace yb
