//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "db/builder.h"

#include <algorithm>
#include <deque>
#include <vector>

#include "db/compaction_iterator.h"
#include "db/dbformat.h"
#include "db/filename.h"
#include "db/internal_stats.h"
#include "db/merge_helper.h"
#include "db/table_cache.h"
#include "db/version_edit.h"
#include "rocksdb/db.h"
#include "rocksdb/env.h"
#include "rocksdb/iterator.h"
#include "rocksdb/options.h"
#include "rocksdb/table.h"
#include "table/block_based_table_builder.h"
#include "table/internal_iterator.h"
#include "util/file_reader_writer.h"
#include "util/iostats_context_imp.h"
#include "util/stop_watch.h"
#include "util/thread_status_util.h"

namespace rocksdb {

class TableFactory;

TableBuilder* NewTableBuilder(
    const ImmutableCFOptions& ioptions,
    const InternalKeyComparator& internal_comparator,
    const std::vector<std::unique_ptr<IntTblPropCollectorFactory>>*
        int_tbl_prop_collector_factories,
    uint32_t column_family_id, WritableFileWriter* file,
    const CompressionType compression_type,
    const CompressionOptions& compression_opts, const bool skip_filters) {
  return ioptions.table_factory->NewTableBuilder(
      TableBuilderOptions(ioptions, internal_comparator,
                          int_tbl_prop_collector_factories, compression_type,
                          compression_opts, skip_filters),
      column_family_id, file);
}

TableBuilder* NewTableBuilder(
    const ImmutableCFOptions& ioptions,
    const InternalKeyComparator& internal_comparator,
    const std::vector<std::unique_ptr<IntTblPropCollectorFactory>>*
        int_tbl_prop_collector_factories,
    uint32_t column_family_id, WritableFileWriter* metadata_file, WritableFileWriter* data_file,
    const CompressionType compression_type,
    const CompressionOptions& compression_opts, const bool skip_filters) {
  return ioptions.table_factory->NewTableBuilder(
      TableBuilderOptions(ioptions, internal_comparator,
          int_tbl_prop_collector_factories, compression_type,
          compression_opts, skip_filters),
      column_family_id, metadata_file, data_file);
}

namespace {
  Status CreateWritableFileWriter(const std::string& filename, const EnvOptions& env_options,
      const Env::IOPriority io_priority, Env* env,
      std::shared_ptr<WritableFileWriter>* file_writer) {
    unique_ptr<WritableFile> file;
    Status s = NewWritableFile(env, filename, &file, env_options);
    if (!s.ok()) {
      return s;
    }
    file->SetIOPriority(io_priority);
    file_writer->reset(new WritableFileWriter(std::move(file), env_options));
    return Status::OK();
  }
} // anonymous namespace

Status BuildTable(
    const std::string& dbname, Env* env, const ImmutableCFOptions& ioptions,
    const EnvOptions& env_options, TableCache* table_cache,
    InternalIterator* iter, FileMetaData* meta,
    const InternalKeyComparator& internal_comparator,
    const std::vector<std::unique_ptr<IntTblPropCollectorFactory>>*
        int_tbl_prop_collector_factories,
    uint32_t column_family_id, std::vector<SequenceNumber> snapshots,
    SequenceNumber earliest_write_conflict_snapshot,
    const CompressionType compression,
    const CompressionOptions& compression_opts, bool paranoid_file_checks,
    InternalStats* internal_stats, const Env::IOPriority io_priority,
    TableProperties* table_properties) {
  // Reports the IOStats for flush for every following bytes.
  const size_t kReportFlushIOStatsEvery = 1048576;
  Status s;
  meta->fd.total_file_size = 0;
  meta->fd.base_file_size = 0;
  iter->SeekToFirst();

  const bool is_split_sst = ioptions.table_factory->IsSplitSstForWriteSupported();

  const std::string base_fname = TableFileName(ioptions.db_paths, meta->fd.GetNumber(),
                                             meta->fd.GetPathId());
  const std::string data_fname = is_split_sst ? TableBaseToDataFileName(base_fname) : "";
  if (iter->Valid()) {
    TableBuilder* builder;
    shared_ptr<WritableFileWriter> base_file_writer;
    shared_ptr<WritableFileWriter> data_file_writer;
    s = CreateWritableFileWriter(base_fname, env_options, io_priority, env, &base_file_writer);
    if (s.ok() && is_split_sst) {
      s = CreateWritableFileWriter(data_fname, env_options, io_priority, env, &data_file_writer);
    }
    if (!s.ok()) {
      return s;
    }
    builder = NewTableBuilder(
        ioptions, internal_comparator, int_tbl_prop_collector_factories,
        column_family_id, base_file_writer.get(), data_file_writer.get(), compression,
        compression_opts);

    MergeHelper merge(env, internal_comparator.user_comparator(),
                      ioptions.merge_operator, nullptr, ioptions.info_log,
                      ioptions.min_partial_merge_operands,
                      true /* internal key corruption is not ok */,
                      snapshots.empty() ? 0 : snapshots.back());

    CompactionIterator c_iter(iter, internal_comparator.user_comparator(),
                              &merge, kMaxSequenceNumber, &snapshots,
                              earliest_write_conflict_snapshot, env,
                              true /* internal key corruption is not ok */);
    c_iter.SeekToFirst();
    for (; c_iter.Valid(); c_iter.Next()) {
      const Slice& key = c_iter.key();
      const Slice& value = c_iter.value();
      builder->Add(key, value);
      meta->UpdateBoundaries(key, c_iter.ikey().sequence);

      // TODO(noetzli): Update stats after flush, too.
      if (io_priority == Env::IO_HIGH &&
          IOSTATS(bytes_written) >= kReportFlushIOStatsEvery) {
        ThreadStatusUtil::SetThreadOperationProperty(
            ThreadStatus::FLUSH_BYTES_WRITTEN, IOSTATS(bytes_written));
      }
    }

    // Finish and check for builder errors
    bool empty = builder->NumEntries() == 0;
    s = c_iter.status();
    if (!s.ok() || empty) {
      builder->Abandon();
    } else {
      s = builder->Finish();
    }

    if (s.ok() && !empty) {
      meta->fd.total_file_size = builder->TotalFileSize();
      meta->fd.base_file_size = builder->BaseFileSize();
      meta->marked_for_compaction = builder->NeedCompact();
      assert(meta->fd.GetTotalFileSize() > 0);
      if (table_properties) {
        *table_properties = builder->GetTableProperties();
      }
    }
    delete builder;

    // Finish and check for file errors
    if (s.ok() && !empty && !ioptions.disable_data_sync) {
      StopWatch sw(env, ioptions.statistics, TABLE_SYNC_MICROS);
      if (is_split_sst) {
        data_file_writer->Sync(ioptions.use_fsync);
      }
      base_file_writer->Sync(ioptions.use_fsync);
    }
    if (s.ok() && !empty && is_split_sst) {
      s = data_file_writer->Close();
    }
    if (s.ok() && !empty) {
      s = base_file_writer->Close();
    }

    if (s.ok() && !empty) {
      // Verify that the table is usable
      std::unique_ptr<InternalIterator> it(table_cache->NewIterator(
          ReadOptions(), env_options, internal_comparator, meta->fd, nullptr,
          (internal_stats == nullptr) ? nullptr
                                      : internal_stats->GetFileReadHist(0),
          false));
      s = it->status();
      if (s.ok() && paranoid_file_checks) {
        for (it->SeekToFirst(); it->Valid(); it->Next()) {
        }
        s = it->status();
      }
    }
  }

  // Check for input iterator errors
  if (!iter->status().ok()) {
    s = iter->status();
  }

  if (!s.ok() || meta->fd.GetTotalFileSize() == 0) {
    env->DeleteFile(base_fname);
    if (is_split_sst) {
      env->DeleteFile(data_fname);
    }
  }
  return s;
}

}  // namespace rocksdb
