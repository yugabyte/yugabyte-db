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

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/split.hpp>

#include "yb/common/doc_hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/consensus/log_index.h"
#include "yb/consensus/log_reader.h"
#include "yb/consensus/log_util.h"

#include "yb/docdb/consensus_frontier.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/value_type.h"

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

#include "yb/util/date_time.h"
#include "yb/util/env.h"
#include "yb/util/env_util.h"
#include "yb/util/enums.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/path_util.h"
#include "yb/util/pb_util.h"
#include "yb/util/result.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/threadpool.h"
#include "yb/util/tostring.h"

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
const std::string kBakExtension = ".bak";
const std::string kIntentsExtension = ".intents";
const std::string kToolName = "Data Patcher";
const std::string kLogPrefix = kToolName + ": ";

#define DATA_PATCHER_ACTIONS (Help)(AddTime)(SubTime)(ApplyPatch)

YB_DEFINE_ENUM(DataPatcherAction, DATA_PATCHER_ACTIONS);

const std::string kHelpDescription = "Show help on command";

struct HelpArguments {
  std::string command;
};

std::unique_ptr<OptionsDescription> HelpOptions() {
  auto result = std::make_unique<OptionsDescriptionImpl<HelpArguments>>(kHelpDescription);
  result->positional.add("command", 1);
  result->hidden.add_options()
      ("command", po::value(&result->args.command));
  return result;
}

CHECKED_STATUS HelpExecute(const HelpArguments& args) {
  if (args.command.empty()) {
    ShowCommands<DataPatcherAction>();
    return Status::OK();
  }

  ShowHelp(VERIFY_RESULT(ActionByName<DataPatcherAction>(args.command)));
  return Status::OK();
}

struct ChangeTimeArguments {
  std::string delta;
  std::string bound_time;
  std::vector<std::string> data_dirs;
  std::vector<std::string> wal_dirs;
  // When concurrency is positive, limit number of concurrent jobs to it.
  int concurrency;
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
           "concurrent jobs is unlimited.");
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
    uint64_t base_file_size;
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
     return std::unique_ptr<rocksdb::TableBuilder>(rocksdb::NewTableBuilder(
        immutable_cf_options, internal_key_comparator_, int_tbl_prop_collector_factories_,
        /* column_family_id= */ 0, base_file_writer, data_file_writer,
        rocksdb::CompressionType::kSnappyCompression, rocksdb::CompressionOptions()));
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
  std::vector<MicrosTime> early_times;

  void HandleTime(HybridTime time) {
    if (time <= bound_time) {
      early_times.push_back(time.GetPhysicalValueMicros());
    }
  }

  void CompleteTime() {
    std::sort(early_times.begin(), early_times.end());
    Unique(&early_times);
  }

  MicrosTime NewEarlyTime(MicrosTime microseconds, bool sst) const {
    auto it = std::lower_bound(early_times.begin(), early_times.end(), microseconds);
    CHECK_EQ(*it, microseconds);
    // We expect that number of entries before bound time in WAL logs would be less than 1e9.
    auto base_micros = bound_time.GetPhysicalValueMicros() + delta.ToMicroseconds() - 1000000000L;
    if (sst) {
      return base_micros - (early_times.end() - it);
    } else {
      return base_micros + (it - early_times.begin());
    }
  }
};

Result<HybridTime> AddDelta(HybridTime ht, const DeltaData& delta_data, bool sst) {
  int64_t microseconds = ht.GetPhysicalValueMicros();
  int64_t bound_microseconds = delta_data.bound_time.GetPhysicalValueMicros();
  if (microseconds <= bound_microseconds) {
    microseconds = delta_data.NewEarlyTime(microseconds, sst);
  } else {
    int64_t delta_microseconds = delta_data.delta.ToMicroseconds();
    SCHECK_GE(static_cast<int64_t>(microseconds - kYugaByteMicrosecondEpoch), -delta_microseconds,
              InvalidArgument, Format("Hybrid time underflow: $0 + $1", ht, delta_data.delta));
    int64_t kMaxMilliseconds = HybridTime::kMax.GetPhysicalValueMicros();
    SCHECK_GT(kMaxMilliseconds - microseconds, delta_microseconds, InvalidArgument,
              Format("Hybrid time overflow: $0 + $1", ht, delta_data.delta));
    microseconds += delta_microseconds;
  }
  return HybridTime::FromMicrosecondsAndLogicalValue(microseconds, ht.GetLogicalValue());
}

Result<DocHybridTime> AddDelta(DocHybridTime doc_ht, const DeltaData& delta_data, bool sst) {
  return DocHybridTime(
      VERIFY_RESULT(AddDelta(doc_ht.hybrid_time(), delta_data, sst)), doc_ht.write_id());
}

Result<char*> AddDeltaToKey(Slice key, const DeltaData& delta_data, std::vector<char>* buffer) {
  auto doc_ht = VERIFY_RESULT(DocHybridTime::DecodeFromEnd(&key));
  buffer->resize(std::max(buffer->size(), key.size() + kMaxBytesPerEncodedHybridTime));
  memcpy(buffer->data(), key.data(), key.size());
  auto new_doc_ht = VERIFY_RESULT(AddDelta(doc_ht, delta_data, /* sst= */ true));
  return new_doc_ht.EncodedInDocDbFormat(buffer->data() + key.size());
}

std::mutex stream_synchronization_mutex;

class SynchronizedCOutWrapper {
 public:
  SynchronizedCOutWrapper() = default;

  ~SynchronizedCOutWrapper() {
    if (!moved_) {
      std::lock_guard<std::mutex> lock(stream_synchronization_mutex);
      std::cout << stream_.str() << std::endl;
    }
  }

  SynchronizedCOutWrapper(const SynchronizedCOutWrapper&) = delete;
  void operator=(const SynchronizedCOutWrapper&) = delete;

  SynchronizedCOutWrapper(SynchronizedCOutWrapper&& rhs) : stream_(std::move(rhs.stream_)) {
    rhs.moved_ = true;
  }

  void operator=(SynchronizedCOutWrapper&& rhs) = delete;

  std::ostream& stream() {
    return stream_;
  }

 private:
  std::ostringstream stream_;
  bool moved_ = false;
};

SynchronizedCOutWrapper sync_cout() {
  return SynchronizedCOutWrapper();
}

template <class T>
SynchronizedCOutWrapper&& operator<<(SynchronizedCOutWrapper&& out, T&& t) {
  out.stream() << std::forward<T>(t);
  return std::move(out);
}

CHECKED_STATUS AddDeltaToSstFile(
    const std::string& fname, MonoDelta delta, HybridTime bound_time, RocksDBHelper* helper) {
  sync_cout() << "Patching: " << fname << ", " << static_cast<const void*>(&fname);

  constexpr size_t kKeySuffixLen = 8;
  bool intents_db = boost::ends_with(DirName(fname), kIntentsExtension);

  auto table_reader = VERIFY_RESULT(helper->NewTableReader(fname));

  rocksdb::ReadOptions read_options;
  std::unique_ptr<rocksdb::InternalIterator> iterator(table_reader->NewIterator(read_options));
  auto data_fname = rocksdb::TableBaseToDataFileName(fname);

  auto out_dir = DirName(fname) + kPatchedExtension;
  auto& env = *Env::Default();
  RETURN_NOT_OK(env.CreateDirs(out_dir));

  auto new_fname = JoinPathSegments(out_dir, BaseName(fname));
  auto new_data_fname = rocksdb::TableBaseToDataFileName(new_fname);
  auto tmp_fname = new_fname + kTmpExtension;
  auto tmp_data_fname = new_data_fname + kTmpExtension;
  size_t num_entries = 0;
  size_t total_file_size = 0;
  {
    auto base_file_writer = VERIFY_RESULT(helper->NewFileWriter(tmp_fname));
    auto data_file_writer = VERIFY_RESULT(helper->NewFileWriter(tmp_data_fname));

    auto builder = helper->NewTableBuilder(base_file_writer.get(), data_file_writer.get());
    bool done = false;
    auto se = ScopeExit([&builder, &done] {
      if (!done) {
        builder->Abandon();
      }
    });

    DeltaData delta_data = {
      .delta = delta,
      .bound_time = bound_time,
    };
    std::vector<char> buffer(0x100);
    for (int step = bound_time ? 0 : 1; step != 2; ++step) {
      for (iterator->SeekToFirst(); iterator->Valid(); iterator->Next()) {
        Slice key = iterator->key();
        auto value_type = static_cast<rocksdb::ValueType>(*(key.end() - kKeySuffixLen));
        if (intents_db && key[0] == docdb::ValueTypeAsChar::kTransactionId) {
          if (key.size() == 1 + TransactionId::StaticSize() + kKeySuffixLen ||
              value_type != rocksdb::ValueType::kTypeValue) {
            if (step) {
              builder->Add(key, iterator->value());
            }
          } else {
            if (step) {
              // Update reverse index entry.
              auto end = VERIFY_RESULT_PREPEND(
                  AddDeltaToKey(iterator->value(), delta_data, &buffer),
                  Format("Intent key $0, value: $1, filename: $2",
                         iterator->key().ToDebugHexString(), iterator->value().ToDebugHexString(),
                         fname));
              builder->Add(key, Slice(buffer.data(), end));
            } else {
              auto value = iterator->value();
              auto doc_ht = VERIFY_RESULT(DocHybridTime::DecodeFromEnd(&value));
              delta_data.HandleTime(doc_ht.hybrid_time());
            }
          }
        } else {
          key.remove_suffix(kKeySuffixLen);
          if (step) {
            // Update intent or regular db entry.
            auto end = VERIFY_RESULT(AddDeltaToKey(key, delta_data, &buffer));
            memcpy(end, key.end(), kKeySuffixLen);
            end += kKeySuffixLen;
            builder->Add(Slice(buffer.data(), end), iterator->value());
          } else {
            auto doc_ht = VERIFY_RESULT(DocHybridTime::DecodeFromEnd(&key));
            delta_data.HandleTime(doc_ht.hybrid_time());
          }
        }
      }

      if (step) {
        done = true;
        RETURN_NOT_OK(builder->Finish());
        num_entries = builder->NumEntries();
        total_file_size = builder->TotalFileSize();
      } else {
        delta_data.CompleteTime();
      }
    }
  }

  RETURN_NOT_OK(env.RenameFile(tmp_data_fname, new_data_fname));
  RETURN_NOT_OK(env.RenameFile(tmp_fname, new_fname));
  sync_cout() << "Generated: " << new_fname << ", with: " << num_entries
              << " entries and size: " << HumanizeBytes(total_file_size);

  return Status::OK();
}

// Takes a list of lists of directories represented as comma-separated strings, and returns the
// combined list of all directories.
std::vector<std::string> PrepareDirs(const std::vector<std::string>& input) {
  std::vector<std::string> dirs;
  for (const auto& dir : input) {
    std::vector<std::string> temp;
    boost::split(temp, dir, boost::is_any_of(","));
    dirs.insert(dirs.end(), temp.begin(), temp.end());
  }
  return dirs;
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

CHECKED_STATUS ChangeTimeInDataFiles(
    MonoDelta delta, HybridTime bound_time, const std::vector<std::string>& dirs,
    TaskRunner* runner) {
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
  for (const auto& fname : files_to_process) {
    runner->Submit([fname, delta, bound_time]() {
      RocksDBHelper helper;
      return AddDeltaToSstFile(fname, delta, bound_time, &helper);
    });
  }
  return Status::OK();
}

CHECKED_STATUS ChangeTimeInWalDir(MonoDelta delta, HybridTime bound_time, const std::string& dir) {
  auto env = Env::Default();
  auto log_index = make_scoped_refptr<log::LogIndex>(dir);
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
  header.mutable_unused_schema();

  RETURN_NOT_OK(new_segment.WriteHeaderAndOpen(header));

  // Set up the new footer. This will be maintained as the segment is written.
  size_t num_entries = 0;
  int64_t min_replicate_index = std::numeric_limits<int64_t>::max();
  int64_t max_replicate_index = std::numeric_limits<int64_t>::min();

  faststring buffer;
  DeltaData delta_data {
    .delta = delta,
    .bound_time = bound_time,
  };
  auto add_delta = [&delta_data](HybridTime ht) -> Result<uint64_t> {
    return VERIFY_RESULT(AddDelta(ht, delta_data, /* sst= */ false)).ToUint64();
  };
  for (int step = bound_time ? 0 : 1; step != 2; ++step) {
    for (const auto& segment : segments) {
      auto read_result = segment->ReadEntries();
      log::LogEntryBatchPB batch;
      for (auto& entry : read_result.entries) {
        auto& replicate = *entry->mutable_replicate();
        auto replicate_ht = HybridTime(replicate.hybrid_time());
        if (step) {
          replicate.set_hybrid_time(VERIFY_RESULT(add_delta(replicate_ht)));
        } else {
          delta_data.HandleTime(replicate_ht);
        }
        if (replicate.has_transaction_state()) {
          auto& state = *replicate.mutable_transaction_state();
          if (state.status() == TransactionStatus::APPLYING) {
            auto commit_ht = HybridTime(state.commit_hybrid_time());
            if (step) {
              state.set_commit_hybrid_time(VERIFY_RESULT(add_delta(commit_ht)));
            } else {
              delta_data.HandleTime(commit_ht);
            }
          }
        } else if (replicate.has_history_cutoff()) {
          auto& state = *replicate.mutable_history_cutoff();
          auto history_cutoff_ht = HybridTime(state.history_cutoff());
          if (step) {
            state.set_history_cutoff(VERIFY_RESULT(add_delta(history_cutoff_ht)));
          } else {
            delta_data.HandleTime(history_cutoff_ht);
          }
        }
        if (step) {
          auto index = entry->replicate().id().index();
          min_replicate_index = std::min(min_replicate_index, index);
          max_replicate_index = std::max(max_replicate_index, index);
          batch.mutable_entry()->AddAllocated(entry.release());
        }
      }
      if (step) {
        read_result.committed_op_id.ToPB(batch.mutable_committed_op_id());
        if (!read_result.entry_metadata.empty()) {
          batch.set_mono_time(read_result.entry_metadata.back().entry_time.ToUInt64());
        }
        buffer.clear();
        pb_util::AppendToString(batch, &buffer);
        num_entries += batch.entry().size();
        RETURN_NOT_OK(new_segment.WriteEntryBatch(Slice(buffer)));
      }
      sync_cout() << "Step " << step << " processed " << batch.entry().size() << " entries in "
                  << segment->path();
    }
    if (!step) {
      delta_data.CompleteTime();
    }
  }

  log::LogSegmentFooterPB footer;
  footer.set_num_entries(num_entries);
  if (num_entries) {
    footer.set_min_replicate_index(min_replicate_index);
    footer.set_max_replicate_index(max_replicate_index);
  }

  RETURN_NOT_OK(new_segment.WriteFooterAndClose(footer));
  return Env::Default()->RenameFile(tmp_segment_path, new_segment_path);
}

CHECKED_STATUS ChangeTimeInWalDirs(
    MonoDelta delta, HybridTime bound_time, const std::vector<std::string>& dirs,
    TaskRunner* runner) {
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
  for (const auto& dir : wal_dirs) {
    runner->Submit([delta, bound_time, dir] {
      return ChangeTimeInWalDir(delta, bound_time, dir);
    });
  }
  return Status::OK();
}

CHECKED_STATUS ChangeTimeExecute(const ChangeTimeArguments& args, bool sub) {
  auto delta = VERIFY_RESULT(DateTime::IntervalFromString(args.delta));
  if (sub) {
    delta = -delta;
  }
  HybridTime bound_time;
  if (!args.bound_time.empty()) {
    bound_time = VERIFY_RESULT(HybridTime::ParseHybridTime(args.bound_time)).AddDelta(-delta);
    if (bound_time < HybridTime::FromMicros(kYugaByteMicrosecondEpoch)) {
      return STATUS_FORMAT(
          InvalidArgument, "Wrong bound-time and delta combination: $0", bound_time);
    }
    sync_cout() << "Bound time before adding delta: " << bound_time.ToString();
  }
  TaskRunner runner;
  RETURN_NOT_OK(runner.Init(args.concurrency));
  RETURN_NOT_OK(ChangeTimeInDataFiles(delta, bound_time, PrepareDirs(args.data_dirs), &runner));
  RETURN_NOT_OK(ChangeTimeInWalDirs(delta, bound_time, PrepareDirs(args.wal_dirs), &runner));
  return runner.Wait();
}

const std::string kAddTimeDescription = "Add time delta to physical time in SST files";

using AddTimeArguments = ChangeTimeArguments;

std::unique_ptr<OptionsDescription> AddTimeOptions() {
  return ChangeTimeOptions(kAddTimeDescription);
}

CHECKED_STATUS AddTimeExecute(const AddTimeArguments& args) {
  return ChangeTimeExecute(args, /* sub= */ false);
}

const std::string kSubTimeDescription = "Subtract time delta from physical time in SST files";

using SubTimeArguments = ChangeTimeArguments;

std::unique_ptr<OptionsDescription> SubTimeOptions() {
  return ChangeTimeOptions(kSubTimeDescription);
}

CHECKED_STATUS SubTimeExecute(const SubTimeArguments& args) {
  return ChangeTimeExecute(args, /* sub= */ true);
}

const std::string kApplyPatchDescription = "Apply prepared SST files patch";

struct ApplyPatchArguments {
  std::vector<std::string> data_dirs;
  std::vector<std::string> wal_dirs;
};

std::unique_ptr<OptionsDescription> ApplyPatchOptions() {
  auto result = std::make_unique<OptionsDescriptionImpl<ApplyPatchArguments>>(
      kApplyPatchDescription);
  result->desc.add_options()
      ("data-dirs", po::value(&result->args.data_dirs)->required(), "TServer data dirs")
      ("wal-dirs", po::value(&result->args.wal_dirs)->required(), "TServer WAL dirs");
  return result;
}

class ApplyPatch {
 public:
  CHECKED_STATUS Execute(const ApplyPatchArguments& args) {
    for (const auto& dir : PrepareDirs(args.data_dirs)) {
      RETURN_NOT_OK(env_->Walk(
          dir, Env::DirectoryOrder::POST_ORDER,
          std::bind(&ApplyPatch::WalkDataCallback, this, _1, _2, _3)));
    }

    for (const auto& dir : PrepareDirs(args.wal_dirs)) {
      RETURN_NOT_OK(env_->Walk(
          dir, Env::DirectoryOrder::POST_ORDER,
          std::bind(&ApplyPatch::WalkWalCallback, this, _1, _2, _3)));
    }

    RocksDBHelper helper;
    auto options = helper.options();
    options.skip_stats_update_on_db_open = true;
    for (const auto* dirs : {&data_dirs_, &wal_dirs_}) {
      for (const auto& dir : *dirs) {
        auto bak_path = dir + kBakExtension;
        auto patched_path = dir + kPatchedExtension;
        if (dirs == &data_dirs_) {
          docdb::RocksDBPatcher patcher(patched_path, options);
          RETURN_NOT_OK(patcher.Load());
          RETURN_NOT_OK(patcher.UpdateFileSizes());
          docdb::ConsensusFrontier frontier;
          frontier.set_hybrid_time(HybridTime::kMin);
          frontier.set_history_cutoff(HybridTime::FromMicros(kYugaByteMicrosecondEpoch));
          RETURN_NOT_OK(patcher.ModifyFlushedFrontier(frontier));
        }
        RETURN_NOT_OK(env_->RenameFile(dir, bak_path));
        RETURN_NOT_OK(env_->RenameFile(patched_path, dir));
        sync_cout() << "Applied patch for: " << dir;
      }
    }

    return Status::OK();
  }

 private:
  CHECKED_STATUS WalkDataCallback(
      Env::FileType type, const std::string& dirname, const std::string& fname) {
    if (type == Env::FileType::FILE_TYPE) {
      return CheckDataFile(dirname, fname);
    } else {
      return CheckDataDirectory(dirname, fname);
    }
  }

  CHECKED_STATUS CheckDataFile(const std::string& dirname, const std::string& fname) {
    if (!regex_match(dirname, kTabletDbRegex)) {
      return Status::OK();
    }
    if (fname != "CURRENT" && !regex_match(fname, kManifestRegex)) {
      return Status::OK();
    }
    auto patched_dirname = dirname + kPatchedExtension;
    if (env_->DirExists(patched_dirname)) {
      auto full_path = JoinPathSegments(dirname, fname);
      sync_cout() << "Copy to patched: " << full_path;
      return env_util::CopyFile(
          env_, full_path, JoinPathSegments(patched_dirname, fname));
    }
    return Status::OK();
  }

  CHECKED_STATUS CheckDataDirectory(const std::string& dirname, const std::string& fname) {
    return CheckDirectory(dirname, fname, &data_dirs_);
  }

  CHECKED_STATUS WalkWalCallback(
      Env::FileType type, const std::string& dirname, const std::string& fname) {
    if (type != Env::FileType::DIRECTORY_TYPE) {
      return Status::OK();
    }
    return CheckDirectory(dirname, fname, &wal_dirs_);
  }

  CHECKED_STATUS CheckDirectory(
      const std::string& dirname, const std::string& fname, std::vector<std::string>* dirs) {
    if (!boost::ends_with(fname, kPatchedExtension)) {
      return Status::OK();
    }
    auto patched_path = JoinPathSegments(dirname, fname);
    auto full_path = patched_path.substr(0, patched_path.size() - kPatchedExtension.size());
    if (!regex_match(full_path, kTabletDbRegex)) {
      return Status::OK();
    }
    dirs->push_back(full_path);
    return Status::OK();
  }

  Env* env_ = Env::Default();
  std::vector<std::string> data_dirs_;
  std::vector<std::string> wal_dirs_;
};

CHECKED_STATUS ApplyPatchExecute(const ApplyPatchArguments& args) {
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
