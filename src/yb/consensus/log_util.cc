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

#include "yb/consensus/log_util.h"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

#include "yb/common/hybrid_time.h"

#include "yb/consensus/consensus.messages.h"
#include "yb/consensus/opid_util.h"
#include "yb/consensus/log.messages.h"
#include "yb/consensus/log_index.h"

#include "yb/fs/fs_manager.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/strings/split.h"
#include "yb/gutil/strings/util.h"

#include "yb/util/atomic.h"
#include "yb/util/coding-inl.h"
#include "yb/util/coding.h"
#include "yb/util/crc.h"
#include "yb/util/debug-util.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/env_util.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/pb_util.h"
#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/result.h"
#include "yb/util/size_literals.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/to_stream.h"

DEFINE_UNKNOWN_int32(log_segment_size_mb, 64,
             "The default segment size for log roll-overs, in MB");
TAG_FLAG(log_segment_size_mb, advanced);

DEFINE_UNKNOWN_uint64(log_segment_size_bytes, 0,
             "The default segment size for log roll-overs, in bytes. "
             "If 0 then log_segment_size_mb is used.");

DEFINE_UNKNOWN_uint64(initial_log_segment_size_bytes, 1024 * 1024,
              "The maximum segment size we want for a new WAL segment, in bytes. "
              "This value keeps doubling (for each subsequent WAL segment) till it gets to the "
              "maximum configured segment size (log_segment_size_bytes or log_segment_size_mb).");

DEFINE_UNKNOWN_bool(durable_wal_write, false,
            "Whether the Log/WAL should explicitly call fsync() after each write.");
TAG_FLAG(durable_wal_write, stable);

DEFINE_UNKNOWN_int32(interval_durable_wal_write_ms, 1000,
            "Interval in ms after which the Log/WAL should explicitly call fsync(). "
            "If 0 fsysnc() is not called.");
TAG_FLAG(interval_durable_wal_write_ms, stable);

DEFINE_UNKNOWN_int32(bytes_durable_wal_write_mb, 1,
             "Amount of data in MB after which the Log/WAL should explicitly call fsync(). "
             "If 0 fsysnc() is not called.");
TAG_FLAG(bytes_durable_wal_write_mb, stable);

DEFINE_UNKNOWN_bool(log_preallocate_segments, true,
            "Whether the WAL should preallocate the entire segment before writing to it");
TAG_FLAG(log_preallocate_segments, advanced);

DEFINE_UNKNOWN_bool(log_async_preallocate_segments, true,
            "Whether the WAL segments preallocation should happen asynchronously");
TAG_FLAG(log_async_preallocate_segments, advanced);

DECLARE_string(fs_data_dirs);

DEFINE_UNKNOWN_bool(require_durable_wal_write, false, "Whether durable WAL write is required."
    "In case you cannot write using O_DIRECT in WAL and data directories and this flag is set true"
    "the system will deliberately crash with the appropriate error. If this flag is set false, "
    "the system will soft downgrade the durable_wal_write flag.");
TAG_FLAG(require_durable_wal_write, stable);

// The target value in the release build is true.
// We set it to false in debug builds to keep testing the old approach in auto tests.
constexpr bool kSaveIndexIntoWalSegmentTargetVal = !yb::kIsDebug;
DEFINE_RUNTIME_AUTO_bool(save_index_into_wal_segments, kLocalPersisted,
    !kSaveIndexIntoWalSegmentTargetVal, kSaveIndexIntoWalSegmentTargetVal,
    "Whether to save log index into WAL segments.");
TAG_FLAG(save_index_into_wal_segments, hidden);
TAG_FLAG(save_index_into_wal_segments, advanced);

DECLARE_bool(store_min_start_ht_running_txns);

namespace yb {
namespace log {

using env_util::ReadFully;
using std::vector;
using std::shared_ptr;
using std::string;
using strings::Substitute;
using strings::SubstituteAndAppend;

const char kTmpSuffix[] = ".tmp";

const char kLogSegmentHeaderMagicString[] = "yugalogf";

// A magic that is written as the very last thing when a segment is closed.
// Segments that were not closed (usually the last one being written) will not
// have this magic.
const char kLogSegmentFooterMagicString[] = "closedls";

// Header is prefixed with the header magic (8 bytes) and the header length (4 bytes).
const size_t kLogSegmentHeaderMagicAndHeaderLength = 12;

// Footer is suffixed with the footer magic (8 bytes) and the footer length (4 bytes).
const size_t kLogSegmentFooterMagicAndFooterLength  = 12;

const size_t kEntryHeaderSize = 12;

const int kLogMajorVersion = 1;
const int kLogMinorVersion = 0;

// Maximum log segment header/footer size, in bytes (8 MB).
const uint32_t kLogSegmentMaxHeaderOrFooterSize = 8 * 1024 * 1024;

LogOptions::LogOptions()
    : segment_size_bytes(FLAGS_log_segment_size_bytes == 0 ? FLAGS_log_segment_size_mb * 1_MB
                                                           : FLAGS_log_segment_size_bytes),
      initial_segment_size_bytes(FLAGS_initial_log_segment_size_bytes),
      durable_wal_write(FLAGS_durable_wal_write),
      interval_durable_wal_write(FLAGS_interval_durable_wal_write_ms > 0 ?
                                     MonoDelta::FromMilliseconds(
                                         FLAGS_interval_durable_wal_write_ms) : MonoDelta()),
      bytes_durable_wal_write_mb(FLAGS_bytes_durable_wal_write_mb),
      preallocate_segments(FLAGS_log_preallocate_segments),
      async_preallocate_segments(FLAGS_log_async_preallocate_segments),
      env(Env::Default()) {
}

Result<scoped_refptr<ReadableLogSegment>> ReadableLogSegment::Open(
    Env* env, const std::string& path) {
  VLOG(1) << "Parsing wal segment: " << path;
  shared_ptr<RandomAccessFile> readable_file;
  RETURN_NOT_OK_PREPEND(env_util::OpenFileForRandom(env, path, &readable_file),
                        "Unable to open file for reading");

  auto segment = make_scoped_refptr<ReadableLogSegment>(path, readable_file);
  if (!VERIFY_RESULT_PREPEND(segment->Init(), "Unable to initialize segment")) {
    return nullptr;
  }
  return segment;
}

ReadableLogSegment::ReadableLogSegment(
    std::string path, shared_ptr<RandomAccessFile> readable_file)
    : path_(std::move(path)),
      file_size_(0),
      readable_to_offset_(0),
      readable_file_(std::move(readable_file)),
      is_initialized_(false),
      footer_was_rebuilt_(false) {
  CHECK_OK(env_util::OpenFileForRandom(Env::Default(), path_, &readable_file_checkpoint_));
}

Status ReadableLogSegment::Init(const LogSegmentHeaderPB& header,
                                const LogSegmentFooterPB& footer,
                                int64_t first_entry_offset) {
  DCHECK(!IsInitialized()) << "Can only call Init() once";
  DCHECK(header.IsInitialized()) << "Log segment header must be initialized";
  DCHECK(footer.IsInitialized()) << "Log segment footer must be initialized";

  RETURN_NOT_OK(ReadFileSize());

  header_.CopyFrom(header);
  footer_.CopyFrom(footer);
  first_entry_offset_ = first_entry_offset;
  is_initialized_ = true;
  readable_to_offset_.store(file_size(), std::memory_order_release);

  return Status::OK();
}

Status ReadableLogSegment::Init(const LogSegmentHeaderPB& header,
                                int64_t first_entry_offset) {
  DCHECK(!IsInitialized()) << "Can only call Init() once";
  DCHECK(header.IsInitialized()) << "Log segment header must be initialized";

  RETURN_NOT_OK(ReadFileSize());

  header_.CopyFrom(header);
  first_entry_offset_ = first_entry_offset;
  is_initialized_ = true;

  // On a new segment, we don't expect any readable entries yet.
  readable_to_offset_.store(first_entry_offset, std::memory_order_release);

  return Status::OK();
}

Result<bool> ReadableLogSegment::Init() {
  DCHECK(!IsInitialized()) << "Can only call Init() once";

  RETURN_NOT_OK(ReadFileSize());

  if (!VERIFY_RESULT(ReadHeader())) {
    return false;
  }

  Status s = ReadFooter();
  if (!s.ok()) {
    LOG(WARNING) << "Could not read footer for segment: " << path_
        << ": " << s.ToString();
  }

  is_initialized_ = true;

  readable_to_offset_.store(file_size(), std::memory_order_release);

  return true;
}

void ReadableLogSegment::UpdateReadableToOffset(const int64_t readable_to_offset) {
  readable_to_offset_.store(readable_to_offset, std::memory_order_release);
  UpdateAtomicMax(&file_size_, readable_to_offset);
}

Status ReadableLogSegment::RebuildFooterByScanning() {
  auto read_entries = ReadEntries();
  RETURN_NOT_OK(read_entries.status);
  return RebuildFooterByScanning(read_entries);
}

Status ReadableLogSegment::RebuildFooterByScanning(const ReadEntriesResult &read_entries) {
  TRACE_EVENT1("log", "ReadableLogSegment::RebuildFooterByScanning",
               "path", path_);

  DCHECK(!footer_.IsInitialized());
  footer_.set_num_entries(read_entries.entries.size());

  uint64_t latest_ht = 0;
  // Rebuild the min/max replicate index (by scanning)
  for (const auto& entry : read_entries.entries) {
    if (entry->has_replicate()) {
      UpdateSegmentFooterIndexes(entry->replicate(), &footer_);
      latest_ht = std::max(latest_ht, entry->replicate().hybrid_time());
    }
  }

  DCHECK(footer_.IsInitialized());
  footer_was_rebuilt_ = true;

  if (latest_ht > 0) {
    footer_.set_close_timestamp_micros(HybridTime(latest_ht).GetPhysicalValueMicros());
  }

  readable_to_offset_.store(read_entries.end_offset, std::memory_order_release);

  LOG(INFO) << "Successfully rebuilt footer for segment: " << path_
            << " (valid entries through byte offset " << read_entries.end_offset << ")";
  return Status::OK();
}

Status ReadableLogSegment::RestoreFooterBuilderAndLogIndex(LogSegmentFooterPB* footer_builder,
                                                           LogIndex* log_index,
                                                           const ReadEntriesResult& read_entries) {
  DCHECK(!footer_builder->IsInitialized());

  footer_builder->set_num_entries(read_entries.entries.size());

  for (size_t entry_idx = 0; entry_idx < read_entries.entries.size(); ++entry_idx) {
    const auto& entry = read_entries.entries[entry_idx];
    if (entry->has_replicate()) {
      UpdateSegmentFooterIndexes(entry->replicate(), footer_builder);
      // Entry might has been already added to log_index_ by PlaySegments()
      // in tablet_bootstrap.cc. If so, it will skip overwriting it.
      const auto& entry_metadata = read_entries.entry_metadata[entry_idx];
      RETURN_NOT_OK(log_index->AddEntry(LogIndexEntry {
        .op_id = OpId::FromPB(entry->replicate().id()),
        .segment_sequence_number = entry_metadata.active_segment_sequence_number,
        .offset_in_segment = entry_metadata.offset,
      }, Overwrite::kFalse));
    }
  }

  // As this is an active segment, set the min_start_time_running_txns as kInvalid since we want CDC
  // to stream records from this segment based on the tablet leader safe time.
  if (GetAtomicFlag(&FLAGS_store_min_start_ht_running_txns)) {
    footer_builder->set_min_start_time_running_txns(HybridTime::kInvalid.ToUint64());
  }

  UpdateReadableToOffset(read_entries.end_offset);
  return Status::OK();
}

Status ReadableLogSegment::CopyTo(
    Env* env, const WritableFileOptions& writable_file_options, const std::string& dest_path,
    const OpId& up_to_op_id) {
  std::unique_ptr<WritableFile> dest_file;
  RETURN_NOT_OK(env->NewWritableFile(writable_file_options, dest_path, &dest_file));
  std::shared_ptr<WritableFile> shared_dest_file(dest_file.release());

  auto dest_segment = std::make_unique<WritableLogSegment>(dest_path, shared_dest_file);
  RETURN_NOT_OK(dest_segment->WriteHeader(header()));
  LogSegmentFooterPB footer;

  const auto temp_log_index_dir = dest_path + "-index.tmp";
  if (env->DirExists(temp_log_index_dir)) {
    RETURN_NOT_OK(env->DeleteRecursively(temp_log_index_dir));
  }
  RETURN_NOT_OK(env->CreateDir(temp_log_index_dir));
  auto log_index = VERIFY_RESULT(LogIndex::NewLogIndex(temp_log_index_dir));

  const auto read_up_to = ReadEntriesUpTo();
  auto offset = first_entry_offset();

  faststring write_buf;
  size_t num_entries = 0;
  uint64_t latest_ht = 0;

  const auto has_op_id_limit = up_to_op_id.valid() && !up_to_op_id.empty();
  bool hit_limit = false;

  LogIndexEntry index_entry;
  index_entry.segment_sequence_number = header().sequence_number();
  while (offset < read_up_to && !hit_limit) {
    index_entry.offset_in_segment = offset;

    if (offset + implicit_cast<ssize_t>(kEntryHeaderSize) >= read_up_to) {
      return STATUS(Corruption, Format("Truncated log entry at offset $0", offset));
    }

    auto current_batch = VERIFY_RESULT(ReadEntryHeaderAndBatch(&offset));

    for (auto it = current_batch->entry().begin(); it != current_batch->entry().end(); ++it) {
      if (!it->has_replicate()) {
        continue;
      }

      const auto op_id = OpId::FromPB(it->replicate().id());
      index_entry.op_id = op_id;

      if (has_op_id_limit) {
        if (OpId::FromPB(current_batch->committed_op_id()) > up_to_op_id) {
          up_to_op_id.ToPB(current_batch->mutable_committed_op_id());
        }

        if (op_id > up_to_op_id) {
          hit_limit = true;
          current_batch->mutable_entry()->erase(it, current_batch->entry().end());
          break;
        }
      }
      UpdateSegmentFooterIndexes(it->replicate(), &footer);
      latest_ht = std::max(latest_ht, it->replicate().hybrid_time());
      RETURN_NOT_OK(log_index->AddEntry(index_entry));
    }

    write_buf.clear();
    write_buf.resize(current_batch->SerializedSize());
    current_batch->SerializeToArray(write_buf.data());
    RETURN_NOT_OK(dest_segment->WriteEntryBatch(write_buf));

    num_entries += current_batch->entry().size();
  }
  footer.set_num_entries(num_entries);
  if (latest_ht > 0) {
    footer.set_close_timestamp_micros(HybridTime(latest_ht).GetPhysicalValueMicros());
  }
  // Note: log_index created here might have holes, because specific WAL segment can have holes due
  // to WAL rewriting. For example, we can have segment with op_ids: { 3.25, 4.26, 5.21 } and footer
  // { num_entries: 3 min_replicate_index: 21 max_replicate_index: 26 index_start_offset: 246 }.
  RETURN_NOT_OK(dest_segment->WriteIndexWithFooterAndClose(log_index.get(), &footer));
  log_index.reset();
  RETURN_NOT_OK(env->DeleteRecursively(temp_log_index_dir));
  return Status::OK();
}

Status ReadableLogSegment::ReadFileSize() {
  // Check the size of the file.
  // Env uses uint here, even though we generally prefer signed ints to avoid
  // underflow bugs. Use a local to convert.
  uint64_t size = VERIFY_RESULT_PREPEND(readable_file_->Size(), "Unable to read file size");
  file_size_.store(size, std::memory_order_release);
  if (size == 0) {
    VLOG(1) << "Log segment file $0 is zero-length: " << path();
    return Status::OK();
  }
  return Status::OK();
}

Result<bool> ReadableLogSegment::ReadHeader() {
  uint32_t header_size;
  RETURN_NOT_OK(ReadHeaderMagicAndHeaderLength(&header_size));
  if (header_size == 0) {
    // If a log file has been pre-allocated but not initialized, then
    // 'header_size' will be 0 even the file size is > 0; in this
    // case, 'is_initialized_' remains set to false and return
    // Status::OK() early. LogReader ignores segments where
    // IsInitialized() returns false.
    return false;
  }

  if (header_size > kLogSegmentMaxHeaderOrFooterSize) {
    return STATUS(Corruption,
        Substitute("File is corrupted. "
                   "Parsed header size: $0 is zero or bigger than max header size: $1",
                   header_size, kLogSegmentMaxHeaderOrFooterSize));
  }

  std::vector<uint8_t> header_space(header_size);
  Slice header_slice;
  LogSegmentHeaderPB header;

  // Read and parse the log segment header.
  RETURN_NOT_OK_PREPEND(ReadFully(readable_file_.get(), kLogSegmentHeaderMagicAndHeaderLength,
                                  header_size, &header_slice, header_space.data()),
                                      "Unable to read fully");

  RETURN_NOT_OK_PREPEND(pb_util::ParseFromArray(&header,
                                                header_slice.data(),
                                                header_size),
                        "Unable to parse protobuf");
  DCHECK(header.IsInitialized()) << "Log segment header must be initialized";

  header_.CopyFrom(header);
  first_entry_offset_ = header_size + kLogSegmentHeaderMagicAndHeaderLength;

  return true;
}


Status ReadableLogSegment::ReadHeaderMagicAndHeaderLength(uint32_t *len) {
  uint8_t scratch[kLogSegmentHeaderMagicAndHeaderLength];
  Slice slice;
  RETURN_NOT_OK(ReadFully(readable_file_.get(), 0, kLogSegmentHeaderMagicAndHeaderLength,
                          &slice, scratch));
  RETURN_NOT_OK(ParseHeaderMagicAndHeaderLength(slice, len));
  return Status::OK();
}

namespace {

// We don't run TSAN on this function because it makes it really slow and causes some
// test timeouts. This is only used on local buffers anyway, so we don't lose much
// by not checking it.
ATTRIBUTE_NO_SANITIZE_THREAD
bool IsAllZeros(const Slice& s) {
  // Walk a pointer through the slice instead of using s[i]
  // since this is way faster in debug mode builds. We also do some
  // manual unrolling for the same purpose.
  const uint8_t* p = s.data();
  size_t rem = s.size();

  while (rem >= 8) {
    if (UNALIGNED_LOAD64(p) != 0) return false;
    rem -= 8;
    p += 8;
  }

  while (rem > 0) {
    if (*p++ != '\0') return false;
    rem--;
  }
  return true;
}
} // anonymous namespace

Status ReadableLogSegment::ParseHeaderMagicAndHeaderLength(const Slice &data,
                                                           uint32_t *parsed_len) {
  RETURN_NOT_OK_PREPEND(data.check_size(kLogSegmentHeaderMagicAndHeaderLength),
                        "Log segment file is too small to contain initial magic number");

  if (memcmp(kLogSegmentHeaderMagicString, data.data(),
             strlen(kLogSegmentHeaderMagicString)) != 0) {
    // As a special case, we check whether the file was allocated but no header
    // was written. We treat that case as an uninitialized file, much in the
    // same way we treat zero-length files.
    // Note: While the above comparison checks 8 bytes, this one checks the full 12
    // to ensure we have a full 12 bytes of NULL data.
    if (IsAllZeros(data)) {
      // 12 bytes of NULLs, good enough for us to consider this a file that
      // was never written to (but apparently preallocated).
      LOG(WARNING) << "Log segment file " << path() << " has 12 initial NULL bytes instead of "
                   << "magic and header length: " << data.ToDebugString()
                   << " and will be treated as a blank segment.";
      *parsed_len = 0;
      return Status::OK();
    }
    // If no magic and not uninitialized, the file is considered corrupt.
    return STATUS(Corruption, Substitute("Invalid log segment file $0: Bad magic. $1",
                                         path(), data.ToDebugString()));
  }

  *parsed_len = DecodeFixed32(data.data() + strlen(kLogSegmentHeaderMagicString));
  return Status::OK();
}

Status ReadableLogSegment::ReadFooter() {
  uint32_t footer_size;
  RETURN_NOT_OK(ReadFooterMagicAndFooterLength(&footer_size));

  if (footer_size == 0 || footer_size > kLogSegmentMaxHeaderOrFooterSize) {
    return STATUS(NotFound,
        Substitute("File is corrupted. "
                   "Parsed header size: $0 is zero or bigger than max header size: $1",
                   footer_size, kLogSegmentMaxHeaderOrFooterSize));
  }

  if (footer_size > (file_size() - first_entry_offset_)) {
    return STATUS(NotFound, "Footer not found. File corrupted. "
        "Decoded footer length pointed at a footer before the first entry.");
  }

  std::vector<uint8_t> footer_space(footer_size);
  Slice footer_slice;

  int64_t footer_offset = file_size() - kLogSegmentFooterMagicAndFooterLength - footer_size;

  LogSegmentFooterPB footer;

  // Read and parse the log segment footer.
  RETURN_NOT_OK_PREPEND(ReadFully(readable_file_.get(), footer_offset,
                                  footer_size, &footer_slice, footer_space.data()),
                        "Footer not found. Could not read fully.");

  RETURN_NOT_OK_PREPEND(pb_util::ParseFromArray(&footer,
                                                footer_slice.data(),
                                                footer_size),
                        "Unable to parse protobuf");

  footer_.Swap(&footer);
  return Status::OK();
}

Status ReadableLogSegment::ReadFooterMagicAndFooterLength(uint32_t *len) {
  uint8_t scratch[kLogSegmentFooterMagicAndFooterLength];
  Slice slice;

  CHECK_GT(file_size(), kLogSegmentFooterMagicAndFooterLength);
  RETURN_NOT_OK(ReadFully(readable_file_.get(),
                          file_size() - kLogSegmentFooterMagicAndFooterLength,
                          kLogSegmentFooterMagicAndFooterLength,
                          &slice,
                          scratch));

  RETURN_NOT_OK(ParseFooterMagicAndFooterLength(slice, len));
  return Status::OK();
}

Status ReadableLogSegment::ParseFooterMagicAndFooterLength(const Slice &data,
                                                           uint32_t *parsed_len) {
  RETURN_NOT_OK_PREPEND(data.check_size(kLogSegmentFooterMagicAndFooterLength),
                        "Slice is too small to contain final magic number");

  if (memcmp(kLogSegmentFooterMagicString, data.data(),
             strlen(kLogSegmentFooterMagicString)) != 0) {
    return STATUS(NotFound, "Footer not found. Footer magic doesn't match");
  }

  *parsed_len = DecodeFixed32(data.data() + strlen(kLogSegmentFooterMagicString));
  return Status::OK();
}

int64_t ReadableLogSegment::ReadEntriesUpTo() {
  if (footer_.IsInitialized() && !footer_was_rebuilt_) {
    // File segment has a footer.
    if (footer_.index_start_offset() > 0) {
      // Read up to start of persisted log index.
      return footer_.index_start_offset();
    }
    return file_size() - footer_.ByteSize() - kLogSegmentFooterMagicAndFooterLength;
  }
  // We likely crashed and always read to the end (or up to recalculated readable_to_offset).
  return readable_to_offset();
}

namespace {

bool ShouldReadEntry(const LWLogEntryPB& entry, const EntriesToRead& entries_to_read) {
  switch (entries_to_read) {
    case EntriesToRead::kAll:
      return true;
    case EntriesToRead::kReplicate:
      return entry.has_replicate();
  }
  FATAL_INVALID_ENUM_VALUE(EntriesToRead, entries_to_read);
}

} // namespace

ReadEntriesResult ReadableLogSegment::ReadEntries(
    int64_t max_entries_to_read,
    EntriesToRead entries_to_read,
    std::optional<int64_t> start_offset_to_read) {
  TRACE_EVENT1("log", "ReadableLogSegment::ReadEntries",
               "path", path_);

  ReadEntriesResult result;

  std::vector<int64_t> recent_offsets(4, -1);
  int64_t batches_read = 0;

  int64_t offset = start_offset_to_read ? *start_offset_to_read : first_entry_offset();

  result.end_offset = offset;

  int64_t num_entries_read = 0;
  int64_t num_entries_found = 0;

  const auto read_up_to = ReadEntriesUpTo();
  VLOG(1) << "Reading segment entries from " << path_
          << ": "
          << YB_EXPR_TO_STREAM_COMMA_SEPARATED(
              first_entry_offset(),
              start_offset_to_read,
              offset,
              file_size(),
              readable_to_offset(),
              read_up_to,
              footer_.ShortDebugString());

  while (offset < read_up_to) {
    const int64_t this_batch_offset = offset;
    recent_offsets[batches_read++ % recent_offsets.size()] = offset;

    // Read and validate the entry header first.
    Status s;
    std::shared_ptr<LWLogEntryBatchPB> current_batch;
    if (offset + implicit_cast<ssize_t>(kEntryHeaderSize) < read_up_to) {
      auto batch_result = ReadEntryHeaderAndBatch(&offset);
      if (batch_result.ok()) {
        current_batch = std::move(*batch_result);
      } else {
        s = batch_result.status();
      }
    } else {
      s = STATUS(Corruption, Substitute("Truncated log entry at offset $0", offset));
    }

    if (PREDICT_FALSE(!s.ok())) {
      if (!s.IsCorruption()) {
        // IO errors should always propagate back
        result.status = s.CloneAndPrepend(Substitute("Error reading from log $0", path_));
        return result;
      }

      auto corrupt_status = MakeCorruptionStatus(
          batches_read, this_batch_offset, &recent_offsets, result.entries, s);

      // If we have a valid footer in the segment, then the segment was correctly
      // closed, and we shouldn't see any corruption anywhere (including the last
      // batch).
      if (HasFooter() && !footer_was_rebuilt_) {
        LOG(WARNING) << "Found a corruption in a closed log segment: " << result.status;

        result.status = corrupt_status.CloneAndPrepend("Log file corruption detected.");

        return result;
      }

      // If we read a corrupt entry, but we don't have a footer, then it's
      // possible that we crashed in the middle of writing an entry.
      // In this case, we scan forward to see if there are any more valid looking
      // entries after this one in the file. If there are, it's really a corruption.
      // if not, we just WARN it, since it's OK for the last entry to be partially
      // written.
      auto has_valid_entries = ScanForValidEntryHeaders(offset);
      if (!has_valid_entries.ok() || *has_valid_entries) {
        string err = "Log file corruption detected.";
        if(!has_valid_entries.ok()) {
          SubstituteAndAppend(&err, " Scanning forward for valid entries failed with $0",
              has_valid_entries.ToString());
        }
        result.status = corrupt_status.CloneAndPrepend(err);
        return result;
      }

      LOG(INFO) << "Ignoring partially flushed segment in write ahead log " << path_
                << " because there are no log entries following this one. "
                << "The server probably crashed in the middle of writing an entry "
                << "to the write-ahead log or downloaded an active log via remote bootstrap. "
                << "Error detail: " << corrupt_status.ToString();
      break;
    }

    VLOG(3) << "Read Log entry batch: " << current_batch->ShortDebugString();
    if (current_batch->has_committed_op_id()) {
      result.committed_op_id = OpId::FromPB(current_batch->committed_op_id());
    }

    for (auto& entry : *current_batch->mutable_entry()) {
      ++num_entries_found;
      if (!ShouldReadEntry(entry, entries_to_read)) {
        continue;
      }
      result.entries.emplace_back(current_batch, &entry);
      DCHECK_NE(current_batch->mono_time(), 0);
      LogEntryMetadata entry_metadata;
      entry_metadata.offset = this_batch_offset;
      entry_metadata.active_segment_sequence_number = header().sequence_number();
      entry_metadata.entry_time = RestartSafeCoarseTimePoint::FromUInt64(
          current_batch->mono_time());
      result.entry_metadata.push_back(std::move(entry_metadata));
      num_entries_read++;
      if (num_entries_read >= max_entries_to_read) {
        break;
      }
    }
    result.end_offset = offset;
    if (num_entries_read >= max_entries_to_read) {
      result.status = Status::OK();
      return result;
    }
  }
  VLOG_WITH_FUNC(1) << "num_entries_found: " << num_entries_found
                    << " num_entries_read: " << num_entries_read;

  if (footer_.IsInitialized() && footer_.num_entries() != num_entries_found) {
    result.status = STATUS_FORMAT(
        Corruption,
        "Found $0 log entries from $1, but expected $2 based on the footer",
        num_entries_read, path_, footer_.num_entries());
  }

  result.status = Status::OK();
  return result;
}

Result<FirstEntryMetadata> ReadableLogSegment::ReadFirstEntryMetadata() {
  auto read_result = ReadEntries(/* max_entries_to_read */ 1);
  const auto& entries = read_result.entries;
  const auto& entry_metadata_records = read_result.entry_metadata;
  if (entries.empty()) {
    return STATUS(NotFound, "No entries found");
  }
  if (entry_metadata_records.empty()) {
    return STATUS(NotFound, "No entry metadata found");
  }
  auto& first_entry = *entries.front();
  if (!first_entry.has_replicate()) {
    return STATUS(NotFound, "No REPLICATE message found in the first entry");
  }

  return FirstEntryMetadata {
    .op_id = OpId::FromPB(first_entry.replicate().id()),
    .entry_time = entry_metadata_records.front().entry_time
  };
}


Result<OpId> ReadableLogSegment::ReadFirstReplicateEntryOpId() {
  auto read_result = ReadEntries(/* max_entries_to_read = */ 1, EntriesToRead::kReplicate);
  const auto& entries = read_result.entries;
  if (entries.empty()) {
    return STATUS(NotFound, "No entries found");
  }
  const auto& first_entry = *entries.front();
  const auto& entry_metadata_records = read_result.entry_metadata;
  RSTATUS_DCHECK(
      !entry_metadata_records.empty(), InternalError,
      Format("No metadata for the first replicate entry in segment $0", path()));
  RSTATUS_DCHECK(
      first_entry.has_replicate(), InternalError,
      Format(
          "No REPLICATE message found in the first replicate entry in segment $0: $1", path(),
          entry_metadata_records.front().offset));
  return OpId::FromPB(first_entry.replicate().id());
}

Result<bool> ReadableLogSegment::ScanForValidEntryHeaders(int64_t offset) {
  TRACE_EVENT1("log", "ReadableLogSegment::ScanForValidEntryHeaders",
               "path", path_);
  LOG(INFO) << "Scanning " << path_ << " for valid entry headers "
            << "following offset " << offset << "...";

  const int kChunkSize = 1024 * 1024;
  std::unique_ptr<uint8_t[]> buf(new uint8_t[kChunkSize]);

  // We overlap the reads by the size of the header, so that if a header
  // spans chunks, we don't miss it.
  for (;
       offset < implicit_cast<int64_t>(file_size() - kEntryHeaderSize);
       offset += kChunkSize - kEntryHeaderSize) {
    auto rem = std::min<int64_t>(file_size() - offset, kChunkSize);
    Slice chunk;
    // If encryption is enabled, need to use checkpoint file to read pre-allocated file since
    // we want to preserve all 0s.
    RETURN_NOT_OK(ReadFully(
        readable_file_checkpoint().get(), offset + readable_file()->GetEncryptionHeaderSize(), rem,
        &chunk, &buf[0]));

    // Optimization for the case where a chunk is all zeros -- this is common in the
    // case of pre-allocated files. This avoids a lot of redundant CRC calculation.
    if (IsAllZeros(chunk)) {
      continue;
    }

    if (readable_file()->IsEncrypted()) {
      // If encryption enabled, decrypt the contents of the file.
      RETURN_NOT_OK(ReadFully(readable_file().get(), offset, rem, &chunk, &buf[0]));
    }

    // Check if this chunk has a valid entry header.
    for (size_t off_in_chunk = 0;
         off_in_chunk < chunk.size() - kEntryHeaderSize;
         off_in_chunk++) {
      const Slice potential_header = Slice(chunk.data() + off_in_chunk, kEntryHeaderSize);

      EntryHeader header;
      if (DecodeEntryHeader(potential_header, &header).ok()) {
        LOG(INFO) << "Found a valid entry header at offset " << (offset + off_in_chunk);
        return true;
      }
    }
  }

  LOG(INFO) << "Found no log entry headers";
  return false;
}

Status ReadableLogSegment::MakeCorruptionStatus(
    size_t batch_number,
    int64_t batch_offset,
    std::vector<int64_t>* recent_offsets,
    const LogEntries& entries,
    const Status& status) const {

  string err = Substitute("Failed trying to read batch #$0 at offset $1 for "
                      "log segment $2: ", batch_number, batch_offset, path_);
  err.append("Prior batch offsets:");
  std::sort(recent_offsets->begin(), recent_offsets->end());
  for (int64_t offset : *recent_offsets) {
    if (offset >= 0) {
      SubstituteAndAppend(&err, " $0", offset);
    }
  }
  if (!entries.empty()) {
    err.append("; Last log entries read:");
    const int kNumEntries = 4; // Include up to the last 4 entries in the segment.
    for (size_t i = std::max(0, static_cast<int>(entries.size()) - kNumEntries);
        i < entries.size(); i++) {
      auto& entry = *entries[i];
      LogEntryTypePB type = entry.type();
      string opid_str;
      if (type == log::REPLICATE && entry.has_replicate()) {
        opid_str = OpId::FromPB(entry.replicate().id()).ToString();
      } else {
        opid_str = "<unknown>";
      }
      SubstituteAndAppend(&err, " [$0 ($1)]", LogEntryTypePB_Name(type), opid_str);
    }
  }

  return status.CloneAndAppend(err);
}

Result<std::shared_ptr<LWLogEntryBatchPB>> ReadableLogSegment::ReadEntryHeaderAndBatch(
    int64_t* offset) {
  EntryHeader header;
  RETURN_NOT_OK(ReadEntryHeader(offset, &header));
  return ReadEntryBatch(offset, header);
}


Status ReadableLogSegment::ReadEntryHeader(int64_t *offset, EntryHeader* header) {
  uint8_t scratch[kEntryHeaderSize];
  Slice slice;
  RETURN_NOT_OK_PREPEND(ReadFully(readable_file().get(), *offset, kEntryHeaderSize,
                                  &slice, scratch),
                        "Could not read log entry header");

  RETURN_NOT_OK(DecodeEntryHeader(slice, header));
  *offset += slice.size();
  return Status::OK();
}

Status ReadableLogSegment::DecodeEntryHeader(const Slice& data, EntryHeader* header) {
  DCHECK_EQ(kEntryHeaderSize, data.size());
  header->msg_length = DecodeFixed32(data.data());
  header->msg_crc = DecodeFixed32(data.data() + 4);
  header->header_crc = DecodeFixed32(data.data() + 8);

  // Verify the header.
  uint32_t computed_crc = crc::Crc32c(data.data(), 8);
  if (computed_crc != header->header_crc) {
    return STATUS_FORMAT(
        Corruption, "Invalid checksum in log entry head header: found=$0, computed=$1",
        header->header_crc, computed_crc);
  }
  return Status::OK();
}


Result<std::shared_ptr<LWLogEntryBatchPB>> ReadableLogSegment::ReadEntryBatch(
    int64_t *offset, const EntryHeader& header) {
  TRACE_EVENT2("log", "ReadableLogSegment::ReadEntryBatch",
               "path", path_,
               "range", Substitute("offset=$0 entry_len=$1",
                                   *offset, header.msg_length));

  if (header.msg_length == 0) {
    return STATUS(Corruption, "Invalid 0 entry length");
  }
  int64_t limit = readable_to_offset();
  if (PREDICT_FALSE(header.msg_length + *offset > limit)) {
    // The log was likely truncated during writing.
    return STATUS(Corruption,
        Substitute("Could not read $0-byte log entry from offset $1 in $2: "
                   "log only readable up to offset $3",
                   header.msg_length, *offset, path_, limit));
  }

  RefCntBuffer buffer(header.msg_length);
  Slice entry_batch_slice;

  Status s = readable_file()->Read(*offset, header.msg_length, &entry_batch_slice, buffer.data());

  if (!s.ok()) {
    return STATUS_FORMAT(
        IOError, "Could not read entry at offset: $0, length: $1. Cause: $2", *offset,
        header.msg_length, s);
  }

  // Verify the CRC.
  uint32_t read_crc = crc::Crc32c(entry_batch_slice.data(), entry_batch_slice.size());
  if (PREDICT_FALSE(read_crc != header.msg_crc)) {
    return STATUS(Corruption, Substitute("Entry CRC mismatch in byte range $0-$1: "
                                         "expected CRC=$2, computed=$3",
                                         *offset, *offset + header.msg_length,
                                         header.msg_crc, read_crc));
  }

  // TODO(lw_uc) embed buffer and first arena block into holder itself.
  struct DataHolder {
    RefCntBuffer buffer;
    ThreadSafeArena arena;

    explicit DataHolder(const RefCntBuffer& buffer_) : buffer(buffer_) {}
  };

  auto holder = std::make_shared<DataHolder>(buffer);
  auto batch = holder->arena.NewArenaObject<LWLogEntryBatchPB>();
  s = batch->ParseFromSlice(entry_batch_slice.Prefix(header.msg_length));

  if (!s.ok()) {
    return STATUS_FORMAT(
        Corruption, "Failed to parse PB at offset: $0, length: $1. Cause: $2", *offset,
        header.msg_length, s);
  }

  *offset += entry_batch_slice.size();
  return rpc::SharedField(holder, batch);
}

const LogSegmentHeaderPB& ReadableLogSegment::header() const {
  DCHECK(header_.IsInitialized());
  return header_;
}

Result<LogIndexBlock> ReadableLogSegment::ReadIndexBlock(
    uint64_t* const offset, faststring* tmp_buf) {
  LogIndexBlockHeaderPB block_header;
  Slice slice;

  const uint64_t start_offset = *offset;

  uint8_t size_buf[sizeof(int64_t)];
  RETURN_NOT_OK(ReadFully(readable_file_.get(), *offset, sizeof(int64_t), &slice, size_buf));
  *offset += sizeof(int64_t);
  auto header_size = static_cast<int64_t>(DecodeFixed64(size_buf));
  RSTATUS_DCHECK_LT(
      header_size, 1_MB, Corruption,
      Format("Got too big header size $0 at offset $1 inside $2", header_size, offset, path()));

  tmp_buf->resize(header_size);
  RETURN_NOT_OK(ReadFully(readable_file_.get(), *offset, header_size, &slice, tmp_buf->data()));
  RSTATUS_DCHECK_EQ(
      slice.size(), header_size, InternalError,
      Format("Header size mismatch at offset $0 inside $1", offset, path()));
  *offset += header_size;
  RETURN_NOT_OK(pb_util::ParseFromArray(&block_header, slice.data(), slice.size()));

  const auto block_size = block_header.size();
  tmp_buf->resize(block_size);
  RETURN_NOT_OK(ReadFully(readable_file_.get(), *offset, block_size, &slice, tmp_buf->data()));
  RSTATUS_DCHECK_EQ(
      slice.size(), block_size, InternalError,
      Format("Block size mismatch at offset $0 inside $1", offset, path()));
  *offset += block_size;

  auto computed_crc = crc::Crc32c(slice.data(), slice.size());
  if (computed_crc != block_header.crc32c()) {
    return STATUS_FORMAT(
        Corruption,
        "Checksum mismatch for log index block of size $0 from file $1 at offset $2: found=$3, "
        "computed=$4",
        block_size, path(), start_offset, block_header.crc32c(), computed_crc);
  }

  return LogIndexBlock {
      .data = slice,
      .start_op_index = block_header.start_op_index(),
      .num_entries = block_header.num_entries(),
      .is_last_block = block_header.is_last_block()
  };
}

WritableLogSegment::WritableLogSegment(string path,
                                       shared_ptr<WritableFile> writable_file)
    : path_(std::move(path)),
      writable_file_(std::move(writable_file)),
      is_header_written_(false),
      is_footer_written_(false) {}

Status WritableLogSegment::ReuseHeader(const LogSegmentHeaderPB& new_header,
                                              const int64_t first_entry_offset) {
  DCHECK(!IsHeaderWritten()) << "Can only open header once";
  DCHECK(new_header.IsInitialized())
      << "Log segment header must be initialized" << new_header.InitializationErrorString();

  header_.CopyFrom(new_header);
  first_entry_offset_ = first_entry_offset;
  is_header_written_ = true;
  DCHECK_GE(written_offset(), first_entry_offset_);
  return Status::OK();
}

Status WritableLogSegment::WriteHeader(const LogSegmentHeaderPB& new_header) {
  DCHECK(!IsHeaderWritten()) << "Can only call WriteHeader() once";
  DCHECK(new_header.IsInitialized())
      << "Log segment header must be initialized" << new_header.InitializationErrorString();
  faststring buf;

  // First the magic.
  buf.append(kLogSegmentHeaderMagicString);
  // Then Length-prefixed header.
  PutFixed32(&buf, new_header.ByteSize());
  // Then Serialize the PB.
  RETURN_NOT_OK(pb_util::AppendToString(new_header, &buf));
  RETURN_NOT_OK(writable_file()->Append(Slice(buf)));

  header_.CopyFrom(new_header);
  first_entry_offset_ = buf.size();
  is_header_written_ = true;
  DCHECK_EQ(written_offset(), first_entry_offset_);

  return Status::OK();
}

Status WritableLogSegment::WriteIndexWithFooterAndClose(
    LogIndex* log_index, LogSegmentFooterPB* footer) {
  TRACE_EVENT1("log", "WritableLogSegment::WriteIndexWithFooterAndClose",
               "path", path_);
  DCHECK(IsHeaderWritten());
  DCHECK(!IsFooterWritten());
  DCHECK(footer->IsInitialized()) << footer->InitializationErrorString();

  if (FLAGS_save_index_into_wal_segments) {
    if (footer->has_min_replicate_index() && footer->has_max_replicate_index()) {
      footer->set_index_start_offset(writable_file()->Size());
      VLOG(3) << "Log segment index start offset: " << footer->index_start_offset();
      RETURN_NOT_OK(
          WriteIndex(log_index, footer->min_replicate_index(), footer->max_replicate_index()));
    } else {
      // This should not happen, but there is no harm in just skipping writing log index into WAL
      // segment file in this case.
      // We will have to read all entries from the segment file to restore index if we need it.
      // This is already implemented inside LogIndex::LazyLoadOneSegment and
      // LogIndex::RebuildFromSegmentEntries because we need it for truncated segment files anyway.
      LOG(WARNING) << "No min/max replicate index in footer for file " << path() << ": "
                   << footer->ShortDebugString();
    }
  }

  faststring buf;
  buf.reserve(footer->ByteSizeLong());
  RETURN_NOT_OK(pb_util::AppendToString(*footer, &buf));

  buf.append(kLogSegmentFooterMagicString);
  PutFixed32(&buf, footer->ByteSize());

  RETURN_NOT_OK_PREPEND(writable_file()->Append(Slice(buf)), "Could not write the footer");

  footer_.CopyFrom(*footer);
  is_footer_written_ = true;

  RETURN_NOT_OK(writable_file_->Close());

  return Status::OK();
}

Status WritableLogSegment::WriteEntryBatch(const Slice& data) {
  DCHECK(is_header_written_);
  DCHECK(!is_footer_written_);
  uint8_t header_buf[kEntryHeaderSize];

  // First encode the length of the message.
  auto len = data.size();
  InlineEncodeFixed32(&header_buf[0], narrow_cast<uint32_t>(len));

  // Then the CRC of the message.
  uint32_t msg_crc = crc::Crc32c(data.data(), data.size());
  InlineEncodeFixed32(&header_buf[4], msg_crc);

  // Then the CRC of the header
  uint32_t header_crc = crc::Crc32c(&header_buf, 8);
  InlineEncodeFixed32(&header_buf[8], header_crc);

  std::array<Slice, 2> slices = {
      Slice(header_buf, sizeof(header_buf)),
      Slice(data),
  };

  // Write the header to the file, followed by the batch data itself.
  RETURN_NOT_OK(writable_file_->AppendSlices(slices.data(), slices.size()));

  return Status::OK();
}

Status WritableLogSegment::TEST_WriteCorruptedEntryBatchAndSync() {
  DCHECK(is_header_written_);
  DCHECK(!is_footer_written_);
  std::string data = "some data";
  uint8_t header_buf[kEntryHeaderSize];

  // First encode the length of the message.
  auto len = data.size();
  InlineEncodeFixed32(&header_buf[0], narrow_cast<uint32_t>(len));

  // Then the CRC of the message.
  uint32_t msg_crc = crc::Crc32c(data.data(), data.size());
  InlineEncodeFixed32(&header_buf[4], msg_crc);

  // Then the CRC of the header.
  uint32_t header_crc = crc::Crc32c(&header_buf, 8);
  InlineEncodeFixed32(&header_buf[8], header_crc);

  // Only append the header.
  RETURN_NOT_OK(writable_file_->Append(Slice(header_buf, sizeof(header_buf))));

  RETURN_NOT_OK(Sync());
  return Status::OK();
}

Status WritableLogSegment::WriteIndex(
    LogIndex* log_index, const int64_t start_index, const int64_t end_index_inclusive) {
  auto block_start_index = start_index;
  while (block_start_index <= end_index_inclusive) {
    auto index_block = VERIFY_RESULT_PREPEND(
        log_index->GetIndexBlock(block_start_index, end_index_inclusive),
        Format(
            "Failed to get index block $0 from to $1: $2", block_start_index, end_index_inclusive));

    RETURN_NOT_OK(WriteIndexBlock(index_block));

    block_start_index += index_block.num_entries;
  }
  return Status::OK();
}

Status WritableLogSegment::WriteIndexBlock(const LogIndexBlock& index_block) {
  LogIndexBlockHeaderPB header;

  header.set_start_op_index(index_block.start_op_index);
  header.set_num_entries(index_block.num_entries);
  header.set_size(index_block.data.size());
  header.set_is_last_block(index_block.is_last_block);
  header.set_crc32c(crc::Crc32c(index_block.data.data(), index_block.data.size()));

  index_block_header_buffer_.reserve(header.ByteSizeLong());

  RETURN_NOT_OK(pb_util::AppendToString(header, &index_block_header_buffer_));

  uint8_t size_buf[sizeof(int64_t)];
  InlineEncodeFixed64(size_buf, index_block_header_buffer_.size());

  RETURN_NOT_OK(writable_file_->Append(Slice(size_buf, sizeof(int64_t))));
  RETURN_NOT_OK(writable_file_->Append(Slice(index_block_header_buffer_)));
  return writable_file_->Append(index_block.data);
}

Status WritableLogSegment::Sync() {
  return writable_file_->Sync();
}

// Creates a LogEntryBatchPB from pre-allocated ReplicateMsgs managed using shared pointers. The
// caller has to ensure these messages are not deleted twice, both by LogEntryBatchPB and by
// the shared pointers.
std::shared_ptr<LWLogEntryBatchPB> CreateBatchFromAllocatedOperations(const ReplicateMsgs& msgs) {
  std::shared_ptr<LWLogEntryBatchPB> result;
  if (msgs.empty()) {
    result = rpc::MakeSharedMessage<LWLogEntryBatchPB>();
  } else {
    auto* batch = msgs.front()->arena().NewObject<LWLogEntryBatchPB>(&msgs.front()->arena());
    result = rpc::SharedField(msgs.front(), batch);
  }
  result->set_mono_time(RestartSafeCoarseMonoClock().Now().ToUInt64());
  for (const auto& msg_ptr : msgs) {
    auto* entry_pb = result->add_entry();
    entry_pb->set_type(log::REPLICATE);
    // entry_pb does not actually own the ReplicateMsg object, even though it thinks it does,
    // because we release it in ~LogEntryBatch. LogEntryBatchPB has a separate vector of shared
    // pointers to messages.
    entry_pb->ref_replicate(msg_ptr.get());
  }
  return result;
}

bool IsLogFileName(const string& fname) {
  if (HasPrefixString(fname, ".")) {
    // Hidden file or ./..
    VLOG(1) << "Ignoring hidden file: " << fname;
    return false;
  }

  if (HasSuffixString(fname, kTmpSuffix)) {
    LOG(WARNING) << "Ignoring tmp file: " << fname;
    return false;
  }

  vector<string> v = strings::Split(fname, "-");
  if (v.size() != 2 || v[0] != FsManager::kWalFileNamePrefix) {
    VLOG(1) << "Not a log file: " << fname;
    return false;
  }

  return true;
}

std::vector<std::string> ParseDirFlags(string flag_dirs, string flag_name) {
  std::vector<std::string> paths = strings::Split(flag_dirs, ",", strings::SkipEmpty());
  return paths;
}

Status CheckPathsAreODirectWritable(const std::vector<std::string> &paths) {
  Env *def_env = Env::Default();
  for (const auto &path : paths) {
    RETURN_NOT_OK(CheckODirectTempFileCreationInDir(def_env, path));
  }
  return Status::OK();
}

Status CheckRelevantPathsAreODirectWritable() {
  if (!FLAGS_log_dir.empty()) {
    RETURN_NOT_OK_PREPEND(CheckPathsAreODirectWritable(ParseDirFlags(
        FLAGS_log_dir, "--log_dir")), "Not all log_dirs are O_DIRECT Writable.");
  }
  RETURN_NOT_OK_PREPEND(CheckPathsAreODirectWritable(ParseDirFlags(
      FLAGS_fs_data_dirs, "--data_dirs")), "Not all fs_data_dirs are O_DIRECT Writable.");

  RETURN_NOT_OK_PREPEND(CheckPathsAreODirectWritable(ParseDirFlags(
      FLAGS_fs_wal_dirs, "--wal_dirs")), "Not all fs_wal_dirs are O_DIRECT Writable.");
  return Status::OK();
}

Status ModifyDurableWriteFlagIfNotODirect() {
  if (FLAGS_durable_wal_write) {
    Status s = CheckRelevantPathsAreODirectWritable();
    if (!s.ok()) {
      if (FLAGS_require_durable_wal_write) {
        // Crash with appropriate error.
        RETURN_NOT_OK_PREPEND(s, "require_durable_wal_write is set true, but O_DIRECT is "
            "not allowed.")
      } else {
        // Report error but do not crash.
        LOG(ERROR) << "O_DIRECT is not allowed in some of the directories. "
            "Setting durable wal write flag to false.";
        FLAGS_durable_wal_write = false;
      }
    }
  }
  return Status::OK();
}

void UpdateSegmentFooterIndexes(
    const consensus::LWReplicateMsg& replicate, LogSegmentFooterPB* footer) {
  const auto index = replicate.id().index();
  const auto log_ht = replicate.hybrid_time();
  if (!footer->has_last_wal_op_log_ht() || !HybridTime(footer->last_wal_op_log_ht()).is_valid() ||
      footer->last_wal_op_log_ht() < log_ht) {
    footer->set_last_wal_op_log_ht(replicate.hybrid_time());
  }

  if (!footer->has_min_replicate_index() || index < footer->min_replicate_index()) {
    footer->set_min_replicate_index(index);
  }
  if (!footer->has_max_replicate_index() || index > footer->max_replicate_index()) {
    footer->set_max_replicate_index(index);
  }
}


}  // namespace log
}  // namespace yb
