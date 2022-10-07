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
#include <iostream>
#include <regex>

#include "yb/common/doc_hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/consensus/log.messages.h"
#include "yb/consensus/log_index.h"
#include "yb/consensus/log_reader.h"
#include "yb/consensus/log_util.h"

#include "yb/docdb/consensus_frontier.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/docdb_types.h"
#include "yb/dockv/value_type.h"
#include "yb/docdb/kv_debug.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/dockv/schema_packing.h"
#include "yb/dockv/value.h"

#include "yb/fs/fs_manager.h"

#include "yb/gutil/stl_util.h"

#include "yb/rocksdb/db/builder.h"
#include "yb/rocksdb/db/dbformat.h"
#include "yb/rocksdb/db/filename.h"
#include "yb/rocksdb/db/version_set.h"
#include "yb/rocksdb/db/writebuffer.h"
#include "yb/rocksdb/table/block_based_table_reader.h"
#include "yb/rocksdb/table/internal_iterator.h"
#include "yb/rocksdb/table/table_builder.h"
#include "yb/rocksdb/util/file_reader_writer.h"

#include "yb/tools/tool_arguments.h"

#include "yb/util/bytes_formatter.h"
#include "yb/util/date_time.h"
#include "yb/util/env.h"
#include "yb/util/env_util.h"
#include "yb/util/enums.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/path_util.h"
#include "yb/util/pb_util.h"
#include "yb/util/random_util.h"
#include "yb/util/result.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/threadpool.h"
#include "yb/util/tostring.h"
#include "yb/util/string_util.h"

using namespace std::placeholders;
namespace po = boost::program_options;

DECLARE_int32(rocksdb_max_background_flushes);
DECLARE_int32(rocksdb_base_background_compactions);
DECLARE_int32(rocksdb_max_background_compactions);
DECLARE_int32(priority_thread_pool_size);

namespace yb {
namespace tools {

const std::regex kTabletRegex(".*/tablet-[0-9a-f]{32}");
const std::regex kTabletDbRegex(".*/tablet-[0-9a-f]{32}(?:\\.intents)?");
const std::regex kManifestRegex("MANIFEST-[0-9]{6}");
const std::string kTmpExtension = ".tmp";
const std::string kPatchedExtension = ".patched";
const std::string kBackupExtension = ".apply-patch-backup";
const std::string kIntentsExtension = ".intents";
const std::string kToolName = "Data Patcher";
const std::string kLogPrefix = kToolName + ": ";

using docdb::StorageDbType;

#define DATA_PATCHER_ACTIONS (Help)(AddTime)(SubTime)(ApplyPatch)

YB_DEFINE_ENUM(DataPatcherAction, DATA_PATCHER_ACTIONS);
YB_DEFINE_ENUM(FileType, (kSST)(kWAL));

// ------------------------------------------------------------------------------------------------
// Help command
// ------------------------------------------------------------------------------------------------

const std::string kHelpDescription = kCommonHelpDescription;

using HelpArguments = CommonHelpArguments;

std::unique_ptr<OptionsDescription> HelpOptions() {
  return CommonHelpOptions();
}

Status HelpExecute(const HelpArguments& args) {
  return CommonHelpExecute<DataPatcherAction>(args);
}

// ------------------------------------------------------------------------------------------------
// Utility functions used add/subtract commands as well as in the apply patch command
// ------------------------------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------
// Common implementation for "add time" and "subtract time" commands
// ------------------------------------------------------------------------------------------------

struct ChangeTimeArguments {
  std::string delta;
  std::string bound_time;
  std::vector<std::string> data_dirs;
  std::vector<std::string> wal_dirs;
  // When concurrency is positive, limit number of concurrent jobs to it.
  // For zero, do not limit the number of concurrent jobs.
  int concurrency = 0;
  size_t max_num_old_wal_entries = 0;
  bool debug = false;

  std::string ToString() {
    return YB_STRUCT_TO_STRING(
        delta, bound_time, data_dirs, wal_dirs, concurrency, max_num_old_wal_entries, debug);
  }
};

std::unique_ptr<OptionsDescription> ChangeTimeOptions(const std::string& caption) {
  auto result = std::make_unique<OptionsDescriptionImpl<ChangeTimeArguments>>(caption);
  auto& args = result->args;
  result->desc.add_options()
      ("delta", po::value(&args.delta)->required(),
       "Delta to add/subtract. For instance 2:30, to use two and a half hour as delta.")
      ("bound-time", po::value(&args.bound_time)->required(),
       "All new time before this bound will be ordered and assigned new time keeping the order. "
           "For instance: 2021-12-02T20:24:07.649298.")
      ("data-dirs", po::value(&args.data_dirs), "TServer data dirs.")
      ("wal-dirs", po::value(&args.wal_dirs),
       "TServer WAL dirs. Not recommended to use while node process is running.")
      ("concurrency", po::value(&args.concurrency)->default_value(-1),
       "Max number of concurrent jobs, if this number is less or equals to 0 then number of "
           "concurrent jobs is unlimited.")
      ("max-num-old-wal-entries",
       po::value(&args.max_num_old_wal_entries)->default_value(1000000000L),
       "Maximum number of WAL entries allowed with timestamps that cannot be shifted by the delta, "
       "and therefore have to be re-mapped differently.")
      ("debug", po::bool_switch(&args.debug),
       "Output detailed debug information. Only practical for a small amount of data.");
  return result;
}

class RocksDBHelper {
 public:
  RocksDBHelper() : immutable_cf_options_(options_) {
    docdb::InitRocksDBOptions(
        &options_, kLogPrefix, /* statistics= */ nullptr, tablet_options_, table_options_);
    internal_key_comparator_ = std::make_shared<rocksdb::InternalKeyComparator>(
        options_.comparator);
  }

  Result<std::unique_ptr<rocksdb::TableReader>> NewTableReader(const std::string& fname) {
    uint64_t base_file_size = 0;
    auto file_reader = VERIFY_RESULT(NewFileReader(fname, &base_file_size));

    std::unique_ptr<rocksdb::TableReader> table_reader;
    RETURN_NOT_OK(rocksdb::BlockBasedTable::Open(
      immutable_cf_options_, env_options_, table_options_, internal_key_comparator_,
      std::move(file_reader), base_file_size, &table_reader
    ));

    auto data_fname = rocksdb::TableBaseToDataFileName(fname);
    auto data_file_reader = VERIFY_RESULT(NewFileReader(data_fname));
    table_reader->SetDataFileReader(std::move(data_file_reader));
    return table_reader;
  }

  std::unique_ptr<rocksdb::TableBuilder> NewTableBuilder(
      rocksdb::WritableFileWriter* base_file_writer,
      rocksdb::WritableFileWriter* data_file_writer) {
      rocksdb::ImmutableCFOptions immutable_cf_options(options_);
     return rocksdb::NewTableBuilder(
        immutable_cf_options, internal_key_comparator_, int_tbl_prop_collector_factories_,
        /* column_family_id= */ 0, base_file_writer, data_file_writer,
        rocksdb::CompressionType::kSnappyCompression, rocksdb::CompressionOptions());
  }

  Result<std::unique_ptr<rocksdb::RandomAccessFileReader>> NewFileReader(
      const std::string& fname, uint64_t* file_size = nullptr) {
    std::unique_ptr<rocksdb::RandomAccessFile> input_file;
    RETURN_NOT_OK(rocksdb::Env::Default()->NewRandomAccessFile(fname, &input_file, env_options_));
    if (file_size) {
      *file_size = VERIFY_RESULT(input_file->Size());
    }
    return std::make_unique<rocksdb::RandomAccessFileReader>(std::move(input_file));
  }

  Result<std::unique_ptr<rocksdb::WritableFileWriter>> NewFileWriter(const std::string& fname) {
    std::unique_ptr<rocksdb::WritableFile> file;
    RETURN_NOT_OK(NewWritableFile(rocksdb::Env::Default(), fname, &file, env_options_));
    return std::make_unique<rocksdb::WritableFileWriter>(std::move(file), env_options_);
  }

  rocksdb::EnvOptions& env_options() {
    return env_options_;
  }

  rocksdb::Options& options() {
    return options_;
  }

 private:
  rocksdb::EnvOptions env_options_;
  tablet::TabletOptions tablet_options_;

  rocksdb::BlockBasedTableOptions table_options_;
  rocksdb::Options options_;
  rocksdb::ImmutableCFOptions immutable_cf_options_;

  rocksdb::InternalKeyComparatorPtr internal_key_comparator_;
  rocksdb::IntTblPropCollectorFactories int_tbl_prop_collector_factories_;
};

struct DeltaData {
  MonoDelta delta;
  HybridTime bound_time;
  size_t max_num_old_wal_entries = 0;

  std::vector<MicrosTime> early_times;
  MicrosTime min_micros_to_keep = 0;
  MicrosTime base_micros = 0;
  bool time_map_ready = false;

  DeltaData(MonoDelta delta_, HybridTime bound_time_, size_t max_num_old_wal_entries_)
      : delta(delta_),
        bound_time(bound_time_),
        max_num_old_wal_entries(max_num_old_wal_entries_) {
    CHECK_GE(max_num_old_wal_entries, 0);
  }

  void AddEarlyTime(HybridTime time) {
    CHECK(!time_map_ready);
    if (time <= bound_time) {
      early_times.push_back(time.GetPhysicalValueMicros());
    }
  }

  void FinalizeTimeMap() {
    std::sort(early_times.begin(), early_times.end());
    Unique(&early_times);

    // All old WAL timestamps will be mapped to
    // [base_micros, bound_time.GetPhysicalValueMicros()), assuming there are no more
    // than max_num_old_wal_entries of them.
    //
    // Old SST timestamps will be left as is if their microsecond component is min_micros_to_keep
    // or less, or mapped to the range [base_micros - early_times.size(), base_micros - 1]
    // otherwise, based on their relative position in the early_times array.

    base_micros = bound_time.GetPhysicalValueMicros() - max_num_old_wal_entries;
    min_micros_to_keep = base_micros - early_times.size() - 1;
    time_map_ready = true;
  }

  HybridTime NewEarlyTime(HybridTime ht, FileType file_type) const {
    CHECK(time_map_ready);
    if (ht.GetPhysicalValueMicros() <= min_micros_to_keep)
      return ht;

    const auto micros = ht.GetPhysicalValueMicros();
    auto it = std::lower_bound(early_times.begin(), early_times.end(), micros);
    CHECK_EQ(*it, micros);
    switch (file_type) {
      case FileType::kSST: {
        auto distance_from_end = early_times.end() - it;
        auto new_micros = base_micros - distance_from_end;
        CHECK_LT(new_micros, base_micros);
        return HybridTime(new_micros, ht.GetLogicalValue());
      }
      case FileType::kWAL: {
        auto distance_from_start = it - early_times.begin();
        auto new_micros = base_micros + distance_from_start;
        CHECK_GE(new_micros, base_micros);
        CHECK_LE(new_micros, bound_time.GetPhysicalValueMicros());
        return HybridTime(new_micros, ht.GetLogicalValue());
      }
    }
    FATAL_INVALID_ENUM_VALUE(FileType, file_type);
  }

  Result<HybridTime> AddDelta(HybridTime ht, FileType file_type) const {
    int64_t microseconds = ht.GetPhysicalValueMicros();
    int64_t bound_microseconds = bound_time.GetPhysicalValueMicros();
    if (microseconds <= bound_microseconds) {
      return NewEarlyTime(ht, file_type);
    }

    int64_t delta_microseconds = delta.ToMicroseconds();
    SCHECK_GE(static_cast<int64_t>(microseconds - kYugaByteMicrosecondEpoch), -delta_microseconds,
              InvalidArgument, Format("Hybrid time underflow: $0 + $1", ht, delta));
    int64_t kMaxMilliseconds = HybridTime::kMax.GetPhysicalValueMicros();
    SCHECK_GT(kMaxMilliseconds - microseconds, delta_microseconds, InvalidArgument,
              Format("Hybrid time overflow: $0 + $1", ht, delta));
    microseconds += delta_microseconds;
    return HybridTime::FromMicrosecondsAndLogicalValue(microseconds, ht.GetLogicalValue());
  }

  Result<DocHybridTime> AddDelta(DocHybridTime doc_ht, FileType file_type) const {
    return DocHybridTime(
        VERIFY_RESULT(AddDelta(doc_ht.hybrid_time(), file_type)), doc_ht.write_id());
  }

  Result<char*> AddDeltaToSstKey(Slice key, std::vector<char>* buffer) const {
    auto doc_ht = VERIFY_RESULT(DocHybridTime::DecodeFromEnd(&key));
    buffer->resize(std::max(buffer->size(), key.size() + kMaxBytesPerEncodedHybridTime));
    memcpy(buffer->data(), key.data(), key.size());
    auto new_doc_ht = VERIFY_RESULT(AddDelta(doc_ht, FileType::kSST));
    return new_doc_ht.EncodedInDocDbFormat(buffer->data() + key.size());
  }

};

Status AddDeltaToSstFile(
    const std::string& fname, MonoDelta delta, HybridTime bound_time,
    size_t max_num_old_wal_entries, bool debug, RocksDBHelper* helper) {
  LOG(INFO) << "Patching: " << fname << ", " << static_cast<const void*>(&fname);

  constexpr size_t kKeySuffixLen = 8;
  const auto storage_db_type =
      boost::ends_with(DirName(fname), kIntentsExtension) ? StorageDbType::kIntents
                                                          : StorageDbType::kRegular;

  auto table_reader = VERIFY_RESULT(helper->NewTableReader(fname));

  rocksdb::ReadOptions read_options;
  std::unique_ptr<rocksdb::InternalIterator> iterator(table_reader->NewIterator(read_options));
  auto data_fname = rocksdb::TableBaseToDataFileName(fname);

  auto out_dir = DirName(fname) + kPatchedExtension;
  auto& env = *Env::Default();
  RETURN_NOT_OK(env.CreateDirs(out_dir));

  const auto new_fname = JoinPathSegments(out_dir, BaseName(fname));
  const auto new_data_fname = rocksdb::TableBaseToDataFileName(new_fname);
  const auto tmp_fname = new_fname + kTmpExtension;
  const auto tmp_data_fname = new_data_fname + kTmpExtension;
  size_t num_entries = 0;
  size_t total_file_size = 0;

  {
    auto base_file_writer = VERIFY_RESULT(helper->NewFileWriter(tmp_fname));
    auto data_file_writer = VERIFY_RESULT(helper->NewFileWriter(tmp_data_fname));

    auto builder = helper->NewTableBuilder(base_file_writer.get(), data_file_writer.get());
    const auto add_kv = [&builder, debug, storage_db_type](const Slice& k, const Slice& v) {
      if (debug) {
        const Slice user_key(k.data(), k.size() - kKeySuffixLen);
        auto key_type = docdb::GetKeyType(user_key, storage_db_type);
        auto rocksdb_value_type = static_cast<rocksdb::ValueType>(*(k.end() - kKeySuffixLen));
        LOG(INFO) << "DEBUG: output KV pair "
                  << "(db_type=" << storage_db_type << ", key_type=" << key_type
                  << ", rocksdb_value_type=" << static_cast<uint64_t>(rocksdb_value_type)
                  << "): " << docdb::DocDBKeyToDebugStr(user_key, storage_db_type) << " => "
                  << docdb::DocDBValueToDebugStr(
                         key_type, user_key, v, nullptr /*schema_packing_provider*/);
      }
      builder->Add(k, v);
    };

    bool done = false;
    auto se = ScopeExit([&builder, &done] {
      if (!done) {
        builder->Abandon();
      }
    });

    DeltaData delta_data(delta, bound_time, max_num_old_wal_entries);
    std::vector<char> buffer(0x100);
    faststring txn_metadata_buffer;

    std::string value_buffer;

    for (int is_final_pass = bound_time ? 0 : 1; is_final_pass != 2; ++is_final_pass) {
      LOG(INFO) << "Performing the " << (is_final_pass ? "final" : "initial") << " pass for "
                << "file " << fname;
      for (iterator->SeekToFirst(); iterator->Valid(); iterator->Next()) {
        const Slice key = iterator->key();
        const auto rocksdb_value_type =
            static_cast<rocksdb::ValueType>(*(key.end() - kKeySuffixLen));
        if (storage_db_type == StorageDbType::kRegular ||
            key[0] != dockv::KeyEntryTypeAsChar::kTransactionId) {
          // Regular DB entry, or a normal intent entry (not txn metadata or reverse index).
          // Update the timestamp at the end of the key.
          const auto key_without_suffix = key.WithoutSuffix(kKeySuffixLen);

          bool value_updated = false;
          if (storage_db_type == StorageDbType::kRegular) {
            dockv::Value docdb_value;
            auto value_slice = iterator->value();
            Slice encoded_intent_doc_ht;
            auto control_fields = VERIFY_RESULT(dockv::ValueControlFields::DecodeWithIntentDocHt(
                &value_slice, &encoded_intent_doc_ht));
            if (!encoded_intent_doc_ht.empty()) {
              auto intent_doc_ht = VERIFY_RESULT(DocHybridTime::FullyDecodeFrom(
                  encoded_intent_doc_ht));
              if (is_final_pass) {
                DocHybridTime new_intent_doc_ht(
                    VERIFY_RESULT(delta_data.AddDelta(intent_doc_ht.hybrid_time(), FileType::kSST)),
                    intent_doc_ht.write_id());
                value_buffer.clear();
                value_buffer.push_back(dockv::KeyEntryTypeAsChar::kHybridTime);
                new_intent_doc_ht.AppendEncodedInDocDbFormat(&value_buffer);
                control_fields.AppendEncoded(&value_buffer);
                value_buffer.append(value_slice.cdata(), value_slice.size());
                value_updated = true;
              } else {
                delta_data.AddEarlyTime(intent_doc_ht.hybrid_time());
              }
            }
          }

          if (is_final_pass) {
            auto end = VERIFY_RESULT(delta_data.AddDeltaToSstKey(key_without_suffix, &buffer));
            memcpy(end, key_without_suffix.end(), kKeySuffixLen);
            end += kKeySuffixLen;
            add_kv(Slice(buffer.data(), end), value_updated ? value_buffer : iterator->value());
          } else {
            Slice key_without_suffix_copy = key_without_suffix;
            auto doc_ht = VERIFY_RESULT(DocHybridTime::DecodeFromEnd(&key_without_suffix_copy));
            delta_data.AddEarlyTime(doc_ht.hybrid_time());
          }
          continue;
        }

        // Now we know this is the intents DB and the key starts with a transaction id.
        // In this case, we only modify the value, and never modify the key.

        if (rocksdb_value_type != rocksdb::ValueType::kTypeValue) {
          if (is_final_pass) {
            add_kv(key, iterator->value());
          }
          continue;
        }

        // Transaction metadata record.
        if (key.size() == 1 + TransactionId::StaticSize() + kKeySuffixLen) {
          // We do not modify the key in this case, only the value.

          // Modify transaction start time stored in metadata.
          TransactionMetadataPB metadata_pb;
          const auto v = iterator->value();
          if (!metadata_pb.ParseFromArray(v.data(), narrow_cast<int>(v.size()))) {
            return STATUS_FORMAT(Corruption, "Bad txn metadata: $0", v.ToDebugHexString());
          }

          if (is_final_pass) {
            const auto new_start_ht = VERIFY_RESULT(delta_data.AddDelta(
                HybridTime(metadata_pb.start_hybrid_time()), FileType::kSST));
            metadata_pb.set_start_hybrid_time(new_start_ht.ToUint64());
            txn_metadata_buffer.clear();
            RETURN_NOT_OK(pb_util::SerializeToString(metadata_pb, &txn_metadata_buffer));
            add_kv(key, txn_metadata_buffer);
          } else {
            delta_data.AddEarlyTime(HybridTime(metadata_pb.start_hybrid_time()));
          }
          continue;
        }

        // Transaction reverse index. We do not modify the timestamp at the end of the key because
        // it does not matter if it is shifted, only the relative order of those timestamps matters.
        // We do modify the timestamp stored at the end of the encoded value, because the value is
        // the intent key.
        if (is_final_pass) {
          auto value_end = VERIFY_RESULT_PREPEND(
              delta_data.AddDeltaToSstKey(iterator->value(), &buffer),
              Format(
                  "Intent key $0, value: $1, filename: $2", iterator->key().ToDebugHexString(),
                  iterator->value().ToDebugHexString(), fname));
          add_kv(iterator->key(), Slice(buffer.data(), value_end));
        } else {
          auto value = iterator->value();
          auto doc_ht_result = DocHybridTime::DecodeFromEnd(&value);
          if (!doc_ht_result.ok()) {
            LOG(INFO) << "Failed to decode hybrid time from the end of value for "
                      << "key " << key.ToDebugHexString() << " (" << FormatSliceAsStr(key) << "), "
                      << "value " << value.ToDebugHexString() << " (" << FormatSliceAsStr(value)
                      << "), "
                      << "decoded value "
                      << DocDBValueToDebugStr(
                             docdb::KeyType::kReverseTxnKey, iterator->key(), iterator->value(),
                             nullptr /*schema_packing_provider*/);
            return doc_ht_result.status();
          }
          delta_data.AddEarlyTime(doc_ht_result->hybrid_time());
        }
      }
      RETURN_NOT_OK(iterator->status());

      if (is_final_pass) {
        done = true;
        RETURN_NOT_OK(builder->Finish());
        num_entries = builder->NumEntries();
        total_file_size = builder->TotalFileSize();
      } else {
        delta_data.FinalizeTimeMap();
      }
    }
  }

  RETURN_NOT_OK(env.RenameFile(tmp_data_fname, new_data_fname));
  RETURN_NOT_OK(env.RenameFile(tmp_fname, new_fname));
  LOG(INFO) << "Generated: " << new_fname << ", with: " << num_entries
            << " entries and size: " << HumanizeBytes(total_file_size);

  return Status::OK();
}

// Checks if the given file in the given directory is an SSTable file that we need to process.
// That means it belong to a valid tablet directory and that we haven't processed it before.
void CheckDataFile(
    const std::string& dirname, const std::string& fname, std::vector<std::string>* out) {
  if (!regex_match(dirname, kTabletDbRegex)) {
    return;
  }
  if (!boost::ends_with(fname, ".sst")) {
    return;
  }
  if (Env::Default()->FileExists(JoinPathSegments(dirname + kPatchedExtension, fname))) {
    return;
  }
  auto full_path = JoinPathSegments(dirname, fname);
  out->push_back(full_path);
}

Status ChangeTimeInDataFiles(
    MonoDelta delta, HybridTime bound_time, size_t max_num_old_wal_entries,
    const std::vector<std::string>& dirs, bool debug, TaskRunner* runner) {
  std::vector<std::string> files_to_process;
  Env* env = Env::Default();
  auto callback = [&files_to_process, env](
      Env::FileType type, const std::string& dirname, const std::string& fname) -> Status {
    if (type == Env::FileType::FILE_TYPE) {
      CheckDataFile(dirname, fname, &files_to_process);
    } else {
      auto full_path = JoinPathSegments(dirname, fname);
      if (regex_match(full_path, kTabletDbRegex)) {
        RETURN_NOT_OK(env->CreateDirs(full_path + kPatchedExtension));
      }
    }
    return Status::OK();
  };
  for (const auto& dir : dirs) {
    RETURN_NOT_OK(env->Walk(dir, Env::DirectoryOrder::POST_ORDER, callback));
  }
  std::shuffle(files_to_process.begin(), files_to_process.end(), ThreadLocalRandom());
  for (const auto& fname : files_to_process) {
    runner->Submit([fname, delta, bound_time, max_num_old_wal_entries, debug]() {
      RocksDBHelper helper;
      return AddDeltaToSstFile(fname, delta, bound_time, max_num_old_wal_entries, debug, &helper);
    });
  }
  return Status::OK();
}

Status ChangeTimeInWalDir(
    MonoDelta delta, HybridTime bound_time, size_t max_num_old_wal_entries,
    const std::string& dir) {
  auto env = Env::Default();
  auto log_index = VERIFY_RESULT(log::LogIndex::NewLogIndex(dir));
  std::unique_ptr<log::LogReader> log_reader;
  RETURN_NOT_OK(log::LogReader::Open(
      env, log_index, kLogPrefix, dir, /* table_metric_entity= */ nullptr,
      /* tablet_metric_entity= */ nullptr, &log_reader));
  log::SegmentSequence segments;
  RETURN_NOT_OK(log_reader->GetSegmentsSnapshot(&segments));
  auto patched_dir = dir + kPatchedExtension;
  RETURN_NOT_OK(env->CreateDirs(patched_dir));
  auto new_segment_path = JoinPathSegments(patched_dir, FsManager::GetWalSegmentFileName(1));
  auto tmp_segment_path = new_segment_path + kTmpExtension;
  std::shared_ptr<WritableFile> new_segment_file;
  {
    std::unique_ptr<WritableFile> writable_file;
    RETURN_NOT_OK(env->NewWritableFile(tmp_segment_path, &writable_file));
    new_segment_file = std::move(writable_file);
  }
  log::WritableLogSegment new_segment(/* path= */ "", new_segment_file);

  // Set up the new header and footer.
  log::LogSegmentHeaderPB header;
  header.set_major_version(log::kLogMajorVersion);
  header.set_minor_version(log::kLogMinorVersion);
  header.set_sequence_number(1);
  header.set_unused_tablet_id("TABLET ID");
  header.mutable_deprecated_schema();

  RETURN_NOT_OK(new_segment.WriteHeader(header));

  // Set up the new footer. This will be maintained as the segment is written.
  size_t num_entries = 0;
  int64_t min_replicate_index = std::numeric_limits<int64_t>::max();
  int64_t max_replicate_index = std::numeric_limits<int64_t>::min();

  faststring buffer;
  DeltaData delta_data(delta, bound_time, max_num_old_wal_entries);
  auto add_delta = [&delta_data](HybridTime ht) -> Result<uint64_t> {
    return VERIFY_RESULT(delta_data.AddDelta(ht, FileType::kWAL)).ToUint64();
  };

  for (int is_final_step = bound_time ? 0 : 1; is_final_step != 2; ++is_final_step) {
    for (const auto& segment : segments) {
      // Read entry batches of a WAL segment, and write as few entry batches as possible, but still
      // make sure that we don't create consecutive entries in the same write batch where the Raft
      // index does not strictly increase. We have a check in log_reader.cc for that.
      // batch only increasing Raft operation indexes.
      auto read_result = segment->ReadEntries();
      log::LogEntryBatchPB batch;
      OpId committed_op_id;
      int64_t last_index = -1;

      auto write_entry_batch = [&batch, &buffer, &num_entries, &new_segment, &read_result,
                                &committed_op_id,
                                &log_index](bool last_batch_of_segment) -> Status {
        if (last_batch_of_segment) {
          read_result.committed_op_id.ToPB(batch.mutable_committed_op_id());
        } else if (committed_op_id.valid()) {
          committed_op_id.ToPB(batch.mutable_committed_op_id());
        }
        if (!read_result.entry_metadata.empty()) {
          batch.set_mono_time(read_result.entry_metadata.back().entry_time.ToUInt64());
        }
        buffer.clear();
        RETURN_NOT_OK(pb_util::AppendToString(batch, &buffer));
        num_entries += batch.entry().size();

        const auto batch_offset = new_segment.written_offset();
        for (const auto& entry_pb : batch.entry()) {
          if (!entry_pb.has_replicate()) {
            continue;
          }

          log::LogIndexEntry index_entry;

          index_entry.op_id = yb::OpId::FromPB(entry_pb.replicate().id());
          index_entry.segment_sequence_number = new_segment.header().sequence_number();
          index_entry.offset_in_segment = batch_offset;
          RETURN_NOT_OK(log_index->AddEntry(index_entry));
        }

        RETURN_NOT_OK(new_segment.WriteEntryBatch(Slice(buffer)));
        batch.clear_entry();
        return Status::OK();
      };

      auto add_entry = [&write_entry_batch, &batch, &last_index, &committed_op_id](
          std::shared_ptr<log::LWLogEntryPB> entry) -> Status {
        if (entry->has_replicate() && entry->replicate().id().index() <= last_index) {
          RETURN_NOT_OK(write_entry_batch(/* last_batch_of_segment= */ false));
        }
        last_index = entry->replicate().id().index();
        if (entry->has_replicate() &&
            entry->replicate().has_committed_op_id()) {
          committed_op_id = OpId::FromPB(entry->replicate().committed_op_id());
        }
        entry->ToGoogleProtobuf(batch.add_entry());
        return Status::OK();
      };

      for (auto& entry : read_result.entries) {
        auto& replicate = *entry->mutable_replicate();
        auto replicate_ht = HybridTime(replicate.hybrid_time());
        if (is_final_step) {
          replicate.set_hybrid_time(VERIFY_RESULT(add_delta(replicate_ht)));
        } else {
          delta_data.AddEarlyTime(replicate_ht);
        }
        if (replicate.has_transaction_state()) {
          auto& state = *replicate.mutable_transaction_state();
          if (state.status() == TransactionStatus::APPLYING) {
            auto commit_ht = HybridTime(state.commit_hybrid_time());
            if (is_final_step) {
              state.set_commit_hybrid_time(VERIFY_RESULT(add_delta(commit_ht)));
            } else {
              delta_data.AddEarlyTime(commit_ht);
            }
          }
        } else if (replicate.has_history_cutoff()) {
          auto& state = *replicate.mutable_history_cutoff();
          if (state.has_primary_cutoff_ht()) {
            auto history_cutoff_ht = HybridTime(state.primary_cutoff_ht());
            if (is_final_step) {
              state.set_primary_cutoff_ht(VERIFY_RESULT(add_delta(history_cutoff_ht)));
            } else {
              delta_data.AddEarlyTime(history_cutoff_ht);
            }
          }
          if (state.has_cotables_cutoff_ht()) {
            auto history_cutoff_ht = HybridTime(state.cotables_cutoff_ht());
            if (is_final_step) {
              state.set_cotables_cutoff_ht(
                  VERIFY_RESULT(add_delta(history_cutoff_ht)));
            } else {
              delta_data.AddEarlyTime(history_cutoff_ht);
            }
          }
        }
        if (is_final_step) {
          auto index = entry->replicate().id().index();
          min_replicate_index = std::min(min_replicate_index, index);
          max_replicate_index = std::max(max_replicate_index, index);
          RETURN_NOT_OK(add_entry(std::move(entry)));
        }
      }

      if (is_final_step) {
        RETURN_NOT_OK(write_entry_batch(/* last_batch_of_segment= */ true));
      }
      LOG(INFO) << "Step " << is_final_step << " processed " << batch.entry().size()
                << " entries in " << segment->path();
    }
    if (!is_final_step) {
      delta_data.FinalizeTimeMap();
    }
  }

  log::LogSegmentFooterPB footer;
  footer.set_num_entries(num_entries);
  if (num_entries) {
    footer.set_min_replicate_index(min_replicate_index);
    footer.set_max_replicate_index(max_replicate_index);
  }

  RETURN_NOT_OK(new_segment.WriteIndexWithFooterAndClose(log_index.get(), &footer));
  return Env::Default()->RenameFile(tmp_segment_path, new_segment_path);
}

Status ChangeTimeInWalDirs(
    MonoDelta delta, HybridTime bound_time, size_t max_num_old_wal_entries,
    const std::vector<std::string>& dirs, TaskRunner* runner) {
  Env* env = Env::Default();
  std::vector<std::string> wal_dirs;
  auto callback = [&wal_dirs](
      Env::FileType type, const std::string& dirname, const std::string& fname) {
    if (type != Env::FileType::DIRECTORY_TYPE) {
      return Status::OK();
    }
    auto full_path = JoinPathSegments(dirname, fname);
    if (!regex_match(full_path, kTabletRegex)) {
      return Status::OK();
    }
    wal_dirs.push_back(full_path);
    return Status::OK();
  };
  for (const auto& dir : dirs) {
    RETURN_NOT_OK(env->Walk(dir, Env::DirectoryOrder::POST_ORDER, callback));
  }
  std::shuffle(wal_dirs.begin(), wal_dirs.end(), ThreadLocalRandom());
  for (const auto& dir : wal_dirs) {
    runner->Submit([delta, bound_time, max_num_old_wal_entries, dir] {
      return ChangeTimeInWalDir(delta, bound_time, max_num_old_wal_entries, dir);
    });
  }
  return Status::OK();
}

Status ChangeTimeExecute(const ChangeTimeArguments& args, bool subtract) {
  auto delta = VERIFY_RESULT(DateTime::IntervalFromString(args.delta));
  if (subtract) {
    delta = -delta;
  }
  HybridTime bound_time;
  if (!args.bound_time.empty()) {
    bound_time = VERIFY_RESULT(HybridTime::ParseHybridTime(args.bound_time)).AddDelta(-delta);
    if (bound_time < HybridTime::FromMicros(kYugaByteMicrosecondEpoch)) {
      return STATUS_FORMAT(
          InvalidArgument, "Wrong bound-time and delta combination: $0", bound_time);
    }
    LOG(INFO) << "Bound time before adding delta: " << bound_time.ToString();
  }
  TaskRunner runner;
  RETURN_NOT_OK(runner.Init(args.concurrency));
  RETURN_NOT_OK(ChangeTimeInDataFiles(
      delta, bound_time, args.max_num_old_wal_entries, SplitAndFlatten(args.data_dirs), args.debug,
      &runner));
  RETURN_NOT_OK(ChangeTimeInWalDirs(
      delta, bound_time, args.max_num_old_wal_entries, SplitAndFlatten(args.wal_dirs), &runner));
  return runner.Wait();
}

// ------------------------------------------------------------------------------------------------
// Add time command
// ------------------------------------------------------------------------------------------------

const std::string kAddTimeDescription = "Add time delta to physical time in SST files";

using AddTimeArguments = ChangeTimeArguments;

std::unique_ptr<OptionsDescription> AddTimeOptions() {
  return ChangeTimeOptions(kAddTimeDescription);
}

Status AddTimeExecute(const AddTimeArguments& args) {
  return ChangeTimeExecute(args, /* subtract= */ false);
}

// ------------------------------------------------------------------------------------------------
// Subtract time command
// ------------------------------------------------------------------------------------------------

const std::string kSubTimeDescription = "Subtract time delta from physical time in SST files";

using SubTimeArguments = ChangeTimeArguments;

std::unique_ptr<OptionsDescription> SubTimeOptions() {
  return ChangeTimeOptions(kSubTimeDescription);
}

Status SubTimeExecute(const SubTimeArguments& args) {
  return ChangeTimeExecute(args, /* subtract= */ true);
}

// ------------------------------------------------------------------------------------------------
// Apply patch
// ------------------------------------------------------------------------------------------------

const std::string kApplyPatchDescription = "Apply prepared SST files patch";

struct ApplyPatchArguments {
  std::vector<std::string> data_dirs;
  std::vector<std::string> wal_dirs;
  bool dry_run = false;
  bool revert = false;
};

std::unique_ptr<OptionsDescription> ApplyPatchOptions() {
  auto result = std::make_unique<OptionsDescriptionImpl<ApplyPatchArguments>>(
      kApplyPatchDescription);
  auto& args = result->args;
  result->desc.add_options()
      ("data-dirs", po::value(&args.data_dirs)->required(), "TServer data dirs")
      ("wal-dirs", po::value(&args.wal_dirs)->required(), "TServer WAL dirs")
      ("dry-run", po::bool_switch(&args.dry_run),
       "Do not make any changes to live data, only check that apply-patch would work. "
       "This might still involve making changes to files in .patched directories.")
      ("revert", po::bool_switch(&args.revert),
       "Revert a previous apply-patch operation. This will move backup RocksDB and WAL "
       "directories back to their live locations, and the live locations to .patched locations.");
  return result;
}

class ApplyPatch {
 public:
  Status Execute(const ApplyPatchArguments& args) {
    dry_run_ = args.dry_run;
    revert_ = args.revert;
    LOG(INFO) << "Running the ApplyPatch command";
    LOG(INFO) << "    data_dirs=" << yb::ToString(args.data_dirs);
    LOG(INFO) << "    wal_dirs=" << yb::ToString(args.data_dirs);
    LOG(INFO) << "    dry_run=" << args.dry_run;
    LOG(INFO) << "    revert=" << args.revert;
    for (const auto& dir : SplitAndFlatten(args.data_dirs)) {
      RETURN_NOT_OK(env_->Walk(
          dir, Env::DirectoryOrder::POST_ORDER,
          std::bind(&ApplyPatch::WalkDataCallback, this, _1, _2, _3)));
    }

    for (const auto& dir : SplitAndFlatten(args.wal_dirs)) {
      RETURN_NOT_OK(env_->Walk(
          dir, Env::DirectoryOrder::POST_ORDER,
          std::bind(&ApplyPatch::WalkWalCallback, this, _1, _2, _3)));
    }

    RocksDBHelper helper;
    auto options = helper.options();
    options.skip_stats_update_on_db_open = true;

    int num_revert_errors = 0;

    int num_dirs_handled = 0;
    for (const auto* dirs : {&data_dirs_, &wal_dirs_}) {
      for (const auto& dir : *dirs) {
        auto backup_path = dir + kBackupExtension;
        auto patched_path = dir + kPatchedExtension;

        if (revert_) {
          bool backup_dir_exists = env_->FileExists(backup_path);
          bool patched_path_exists = env_->FileExists(patched_path);
          if (backup_dir_exists && !patched_path_exists) {
            auto rename_status = ChainRename(backup_path, dir, patched_path);
            if (!rename_status.ok()) {
              LOG(INFO) << "Error during revert: " << rename_status;
              num_revert_errors++;
            }
          } else {
            LOG(INFO)
                << "Not attempting to restore " << backup_path << " to " << dir
                << " after moving " << dir << " back to " << patched_path
                << ": "
                << (backup_dir_exists ? "" : "backup path does not exist; ")
                << (patched_path_exists ? "patched path already exists" : "");
            num_revert_errors++;
          }
          continue;
        }

        if (dirs == &data_dirs_) {
          if (valid_rocksdb_dirs_.count(dir)) {
            LOG(INFO) << "Patching non-live RocksDB metadata in " << patched_path;
            docdb::RocksDBPatcher patcher(patched_path, options);
            RETURN_NOT_OK(patcher.Load());
            RETURN_NOT_OK(patcher.UpdateFileSizes());
            docdb::ConsensusFrontier frontier;
            frontier.set_hybrid_time(HybridTime::kMin);
            frontier.set_history_cutoff_information(
                { HybridTime::FromMicros(kYugaByteMicrosecondEpoch),
                  HybridTime::FromMicros(kYugaByteMicrosecondEpoch) });
            RETURN_NOT_OK(patcher.ModifyFlushedFrontier(frontier));
          } else {
            LOG(INFO) << "We did not see RocksDB CURRENT or MANIFEST-... files in "
                       << dir << ", skipping applying " << patched_path;
            continue;
          }
        }

        RETURN_NOT_OK(ChainRename(patched_path, dir, backup_path));
        num_dirs_handled++;
      }
    }

    LOG(INFO) << "Processed " << num_dirs_handled << " directories (two renames per each)";
    if (num_revert_errors) {
      return STATUS_FORMAT(
          IOError,
          "Encountered $0 errors when trying to revert an applied patch. "
          "Check the log above for details.",
          num_revert_errors);
    }
    return Status::OK();
  }

 private:

  // ----------------------------------------------------------------------------------------------
  // Functions for traversing RocksDB data directories
  // ----------------------------------------------------------------------------------------------

  Status WalkDataCallback(
      Env::FileType type, const std::string& dirname, const std::string& fname) {
    switch (type) {
      case Env::FileType::FILE_TYPE:
        return HandleDataFile(dirname, fname);
      case Env::FileType::DIRECTORY_TYPE:
        CheckDirectory(dirname, fname, &data_dirs_);
        return Status::OK();
    }
    FATAL_INVALID_ENUM_VALUE(Env::FileType, type);
  }

  // Handles a file found during walking through the a data (RocksDB) directory tree. Looks for
  // CURRENT and MANIFEST files and copies them to the corresponding .patched directory. Does not
  // modify live data of the cluster.
  Status HandleDataFile(const std::string& dirname, const std::string& fname) {
    if (revert_) {
      // We don't look at any of the manifest files during the revert operation.
      return Status::OK();
    }

    if (!regex_match(dirname, kTabletDbRegex)) {
      return Status::OK();
    }
    if (fname != "CURRENT" && !regex_match(fname, kManifestRegex)) {
      return Status::OK();
    }
    auto patched_dirname = dirname + kPatchedExtension;
    if (env_->DirExists(patched_dirname)) {
      valid_rocksdb_dirs_.insert(dirname);
      auto full_src_path = JoinPathSegments(dirname, fname);
      auto full_dst_path = JoinPathSegments(patched_dirname, fname);
      LOG(INFO) << "Copying file " << full_src_path << " to " << full_dst_path;
      Status copy_status = env_util::CopyFile(env_, full_src_path, full_dst_path);
      if (!copy_status.ok()) {
        LOG(INFO) << "Error copying file " << full_src_path << " to " << full_dst_path << ": "
                    << copy_status;
      }
      return copy_status;
    }

    LOG(INFO) << "Directory " << patched_dirname << " does not exist, not copying "
                << "the file " << fname << " there (this is not an error)";
    return Status::OK();
  }

  // ----------------------------------------------------------------------------------------------
  // Traversing WAL directories
  // ----------------------------------------------------------------------------------------------

  Status WalkWalCallback(
      Env::FileType type, const std::string& dirname, const std::string& fname) {
    if (type != Env::FileType::DIRECTORY_TYPE) {
      return Status::OK();
    }
    CheckDirectory(dirname, fname, &wal_dirs_);
    return Status::OK();
  }

  // ----------------------------------------------------------------------------------------------
  // Functions used for both RocksDB data and WALs
  // ----------------------------------------------------------------------------------------------

  // Look at the given directory, and if it is a patched directory (or a backup directory if we are
  // doing a revert operation), strip off the suffix and add the corresponding live directory to
  // the given vector.
  void CheckDirectory(
      const std::string& dirname, const std::string& fname, std::vector<std::string>* dirs) {
    const std::string& needed_extension = revert_ ? kBackupExtension : kPatchedExtension;
    if (!boost::ends_with(fname, needed_extension)) {
      return;
    }
    auto patched_path = JoinPathSegments(dirname, fname);
    auto full_path = patched_path.substr(0, patched_path.size() - needed_extension.size());
    if (!regex_match(full_path, kTabletDbRegex)) {
      return;
    }
    dirs->push_back(full_path);
    return;
  }

  // Renames dir1 -> dir2 -> dir3, starting from the end of the chain.
  Status ChainRename(
      const std::string& dir1, const std::string& dir2, const std::string& dir3) {
    RETURN_NOT_OK(SafeRename(dir2, dir3, /* check_dst_collision= */ true));

    // Don't check that dir2 does not exist, because we haven't actually moved dir2 to dir3 in the
    // dry run mode.
    return SafeRename(dir1, dir2, /* check_dst_collision= */ false);
  }

  // A logging wrapper over directory renaming. In dry-run mode, checks for some errors, but
  // check_dst_collision=false allows to skip ensuring that the destination does not exist.
  Status SafeRename(
      const std::string& src, const std::string& dst, bool check_dst_collision) {
    if (dry_run_) {
      if (!env_->FileExists(src)) {
        return STATUS_FORMAT(
            IOError, "Would fail to rename $0 to $1, source does not exist", src, dst);
      }
      if (check_dst_collision && env_->FileExists(dst)) {
        return STATUS_FORMAT(
            IOError, "Would fail to rename $0 to $1, destination already exists", src, dst);
      }
      LOG(INFO) << "Would rename " << src << " to " << dst;
      return Status::OK();
    }
    LOG(INFO) << "Renaming " << src << " to " << dst;
    Status s = env_->RenameFile(src, dst);
    if (!s.ok()) {
      LOG(ERROR) << "Error renaming " << src << " to " << dst << ": " << s;
    }
    return s;
  }

  Env* env_ = Env::Default();
  std::vector<std::string> data_dirs_;
  std::vector<std::string> wal_dirs_;

  // The set of tablet RocksDB directories where we found CURRENT and MANIFEST-... files, indicating
  // that there is a valid RocksDB database present.
  std::set<std::string> valid_rocksdb_dirs_;

  bool dry_run_ = false;
  bool revert_ = false;
};

Status ApplyPatchExecute(const ApplyPatchArguments& args) {
  ApplyPatch apply_patch;
  return apply_patch.Execute(args);
}

YB_TOOL_ARGUMENTS(DataPatcherAction, DATA_PATCHER_ACTIONS);

} // namespace tools
} // namespace yb

int main(int argc, char** argv) {
  yb::HybridTime::TEST_SetPrettyToString(true);

  // Setup flags to avoid unnecessary logging
  FLAGS_rocksdb_max_background_flushes = 1;
  FLAGS_rocksdb_max_background_compactions = 1;
  FLAGS_rocksdb_base_background_compactions = 1;
  FLAGS_priority_thread_pool_size = 1;

  yb::InitGoogleLoggingSafeBasic(argv[0]);
  return yb::tools::ExecuteTool<yb::tools::DataPatcherAction>(argc, argv);
}
