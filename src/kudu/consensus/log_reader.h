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
#ifndef KUDU_LOG_LOG_READER_H_
#define KUDU_LOG_LOG_READER_H_

#include <gtest/gtest.h>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "kudu/consensus/log_metrics.h"
#include "kudu/consensus/log_util.h"
#include "kudu/consensus/opid_util.h"
#include "kudu/fs/fs_manager.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/gutil/spinlock.h"
#include "kudu/util/locks.h"

namespace kudu {
namespace log {
class Log;
class LogIndex;
struct LogIndexEntry;

// Reads a set of segments from a given path. Segment headers and footers
// are read and parsed, but entries are not.
// This class is thread safe.
class LogReader {
 public:
  ~LogReader();

  // Opens a LogReader on the default tablet log directory, and sets
  // 'reader' to the newly created LogReader.
  //
  // 'index' may be NULL, but if it is, ReadReplicatesInRange() may not
  // be used.
  static Status Open(FsManager *fs_manager,
                     const scoped_refptr<LogIndex>& index,
                     const std::string& tablet_id,
                     const scoped_refptr<MetricEntity>& metric_entity,
                     gscoped_ptr<LogReader> *reader);

  // Opens a LogReader on a specific tablet log recovery directory, and sets
  // 'reader' to the newly created LogReader.
  static Status OpenFromRecoveryDir(FsManager *fs_manager,
                                    const std::string& tablet_id,
                                    const scoped_refptr<MetricEntity>& metric_entity,
                                    gscoped_ptr<LogReader> *reader);

  // Returns the biggest prefix of segments, from the current sequence, guaranteed
  // not to include any replicate messages with indexes >= 'index'.
  Status GetSegmentPrefixNotIncluding(int64_t index,
                                      SegmentSequence* segments) const;

  // Return the minimum replicate index that is retained in the currently available
  // logs. May return -1 if no replicates have been logged.
  int64_t GetMinReplicateIndex() const;

  // Returns a map of maximum log index in segment -> segment size representing all the segments
  // that start after 'min_op_idx', up to 'segments_count'.
  //
  // 'min_op_idx' is the minimum operation index to start looking from, we don't record
  // the segments before the one that contain that id.
  //
  // 'segments_count' is the number of segments we'll add to the map. It _must_ be sized so that
  // we don't add the last segment. If we find logs that can be GCed, we'll decrease the number of
  // elements we'll add to the map by 1 since they.
  //
  // 'max_close_time_us' is the timestamp in microseconds from which we don't want to evict,
  // meaning that log segments that we closed after that time must not be added to the map.
  void GetMaxIndexesToSegmentSizeMap(int64_t min_op_idx, int32_t segments_count,
                                     int64_t max_close_time_us,
                                     std::map<int64_t, int64_t>* max_idx_to_segment_size) const;

  // Return a readable segment with the given sequence number, or NULL if it
  // cannot be found (e.g. if it has already been GCed).
  scoped_refptr<ReadableLogSegment> GetSegmentBySequenceNumber(int64_t seq) const;

  // Copies a snapshot of the current sequence of segments into 'segments'.
  // 'segments' will be cleared first.
  Status GetSegmentsSnapshot(SegmentSequence* segments) const;

  // Reads all ReplicateMsgs from 'starting_at' to 'up_to' both inclusive.
  // The caller takes ownership of the returned ReplicateMsg objects.
  //
  // Will attempt to read no more than 'max_bytes_to_read', unless it is set to
  // LogReader::kNoSizeLimit. If the size limit would prevent reading any operations at
  // all, then will read exactly one operation.
  //
  // Requires that a LogIndex was passed into LogReader::Open().
  Status ReadReplicatesInRange(
      const int64_t starting_at,
      const int64_t up_to,
      int64_t max_bytes_to_read,
      std::vector<consensus::ReplicateMsg*>* replicates) const;
  static const int kNoSizeLimit;

  // Look up the OpId for the given operation index.
  // Returns a bad Status if the log index fails to load (eg. due to an IO error).
  Status LookupOpId(int64_t op_index, consensus::OpId* op_id) const;

  // Returns the number of segments.
  const int num_segments() const;

  std::string ToString() const;

 private:
  FRIEND_TEST(LogTest, TestLogReader);
  FRIEND_TEST(LogTest, TestReadLogWithReplacedReplicates);
  friend class Log;
  friend class LogTest;

  enum State {
    kLogReaderInitialized,
    kLogReaderReading,
    kLogReaderClosed
  };

  // Appends 'segment' to the segments available for read by this reader.
  // Index entries in 'segment's footer will be added to the index.
  // If the segment has no footer it will be scanned so this should not be used
  // for new segments.
  Status AppendSegment(const scoped_refptr<ReadableLogSegment>& segment);

  // Same as above but for segments without any entries.
  // Used by the Log to add "empty" segments.
  Status AppendEmptySegment(const scoped_refptr<ReadableLogSegment>& segment);

  // Removes segments with sequence numbers less than or equal to 'seg_seqno' from this reader.
  Status TrimSegmentsUpToAndIncluding(int64_t seg_seqno);

  // Replaces the last segment in the reader with 'segment'.
  // Used to replace a segment that was still in the process of being written
  // with its complete version which has a footer and index entries.
  // Requires that the last segment in 'segments_' has the same sequence
  // number as 'segment'.
  // Expects 'segment' to be properly closed and to have footer.
  Status ReplaceLastSegment(const scoped_refptr<ReadableLogSegment>& segment);

  // Appends 'segment' to the segment sequence.
  // Assumes that the segment was scanned, if no footer was found.
  // To be used only internally, clients of this class with private access (i.e. friends)
  // should use the thread safe version, AppendSegment(), which will also scan the segment
  // if no footer is present.
  Status AppendSegmentUnlocked(const scoped_refptr<ReadableLogSegment>& segment);

  // Used by Log to update its LogReader on how far it is possible to read
  // the current segment. Requires that the reader has at least one segment
  // and that the last segment has no footer, meaning it is currently being
  // written to.
  void UpdateLastSegmentOffset(int64_t readable_to_offset);

  // Read the LogEntryBatch pointed to by the provided index entry.
  // 'tmp_buf' is used as scratch space to avoid extra allocation.
  Status ReadBatchUsingIndexEntry(const LogIndexEntry& index_entry,
                                  faststring* tmp_buf,
                                  gscoped_ptr<LogEntryBatchPB>* batch) const;

  LogReader(FsManager* fs_manager, const scoped_refptr<LogIndex>& index,
            std::string tablet_name,
            const scoped_refptr<MetricEntity>& metric_entity);

  // Reads the headers of all segments in 'path_'.
  Status Init(const std::string& path_);

  // Initializes an 'empty' reader for tests, i.e. does not scan a path looking for segments.
  Status InitEmptyReaderForTests();

  FsManager *fs_manager_;
  const scoped_refptr<LogIndex> log_index_;
  const std::string tablet_id_;

  // Metrics
  scoped_refptr<Counter> bytes_read_;
  scoped_refptr<Counter> entries_read_;
  scoped_refptr<Histogram> read_batch_latency_;

  // The sequence of all current log segments in increasing sequence number
  // order.
  SegmentSequence segments_;

  mutable simple_spinlock lock_;

  State state_;

  DISALLOW_COPY_AND_ASSIGN(LogReader);
};

}  // namespace log
}  // namespace kudu

#endif /* KUDU_LOG_LOG_READER_H_ */
