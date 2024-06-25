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

#include <vector>

#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/stringize.hpp>

#include "yb/common/opid.h"
#include "yb/common/schema_pbutil.h"
#include "yb/common/schema.h"
#include "yb/common/transaction.h"

#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/log.h"
#include "yb/consensus/log.messages.h"
#include "yb/consensus/log_index.h"
#include "yb/consensus/log_reader.h"

#include "yb/docdb/docdb_types.h"
#include "yb/dockv/doc_key.h"
#include "yb/docdb/kv_debug.h"

#include "yb/gutil/strings/numbers.h"

#include "yb/util/env.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/metric_entity.h"
#include "yb/util/pb_util.h"
#include "yb/util/result.h"
#include "yb/util/slice.h"
#include "yb/util/size_literals.h"
#include "yb/util/status_format.h"

DEFINE_NON_RUNTIME_bool(print_headers, true, "print the log segment headers/footers");
DEFINE_NON_RUNTIME_bool(filter_log_segment, false, "filter the input log segment");
DEFINE_NON_RUNTIME_string(print_entries, "decoded",
              "How to print entries:\n"
              "  false|0|no = don't print\n"
              "  true|1|yes|decoded = print them decoded\n"
              "  pb = print the raw protobuf\n"
              "  id = print only their ids");
DEFINE_NON_RUNTIME_int32(truncate_data, 100,
             "Truncate the data fields to the given number of bytes "
             "before printing. Set to 0 to disable");

DEFINE_NON_RUNTIME_int64(min_op_term_to_omit, yb::OpId::Invalid().term,
             "Term of first record (inclusive) to omit from the result for --filter_log_segment");

DEFINE_NON_RUNTIME_int64(min_op_index_to_omit, yb::OpId::Invalid().index,
             "Index of first record (inclusive) to omit from the result for --filter_log_segment");

DEFINE_NON_RUNTIME_int64(max_op_term_to_omit, yb::OpId::Invalid().term,
             "Term of last record (inclusive) to omit from the result for --filter_log_segment");

DEFINE_NON_RUNTIME_int64(max_op_index_to_omit, yb::OpId::Invalid().index,
             "Index of last record (inclusive) to omit from the result for --filter_log_segment");

DEFINE_NON_RUNTIME_string(output_wal_dir, "",
             "WAL directory for the output of --filter_log_segment");

namespace yb {
namespace log {

using consensus::ReplicateMsg;
using std::string;
using std::cout;
using std::endl;

enum PrintEntryType {
  DONT_PRINT,
  PRINT_PB,
  PRINT_DECODED,
  PRINT_ID
};

static PrintEntryType ParsePrintType() {
  if (!ParseLeadingBoolValue(FLAGS_print_entries.c_str(), true)) {
    return DONT_PRINT;
  }

  if (ParseLeadingBoolValue(FLAGS_print_entries.c_str(), false) ||
      FLAGS_print_entries == "decoded") {
    return PRINT_DECODED;
  }

  if (FLAGS_print_entries == "pb") {
    return PRINT_PB;
  }

  if (FLAGS_print_entries == "id") {
    return PRINT_ID;
  }

  LOG(FATAL) << "Unknown value for --print_entries: " << FLAGS_print_entries;
}

void PrintIdOnly(const LogEntryPB& entry) {
  switch (entry.type()) {
    case log::REPLICATE:
    {
      HybridTime ht(entry.replicate().hybrid_time());
      cout << "  id {" << endl;
      cout << "    term: " << entry.replicate().id().term() << endl;
      cout << "    index: " << entry.replicate().id().index() << endl;
      cout << "  }" << endl;
      cout << "  hybrid_time: " << ht.ToDebugString() << endl;
      cout << "  op_type: " << OperationType_Name(entry.replicate().op_type()) << endl;
      cout << "  size: " << entry.replicate().ByteSizeLong();
      break;
    }
    default:
      cout << "UNKNOWN: " << entry.ShortDebugString();
  }

  cout << endl;
}

Status PrintDecodedWriteRequestPB(const string& indent, const tablet::WritePB& write) {
  cout << indent << "write {" << endl;
  if (write.has_external_hybrid_time()) {
    HybridTime ht(write.external_hybrid_time());
    cout << indent << indent << "external_hybrid_time: " << ht.ToDebugString() << endl;
  }
  if (write.has_write_batch()) {
    if (write.has_unused_tablet_id()) {
      cout << indent << indent << "unused_tablet_id: " << write.unused_tablet_id() << endl;
    }
    const ::yb::docdb::KeyValueWriteBatchPB& write_batch = write.write_batch();
    cout << indent << indent << "write_batch {" << endl;
    cout << indent << indent << indent << "write_pairs_size: " << write_batch.write_pairs_size()
         << endl;
    // write tablet id
    for (int i = 0; i < write_batch.write_pairs_size(); i++) {
      cout << indent << indent << indent << "write_pairs {" << endl;
      const ::yb::docdb::KeyValuePairPB& kv = write_batch.write_pairs(i);
      if (kv.has_key()) {
        Result<std::string> formatted_key = DocDBKeyToDebugStr(
            kv.key(), ::yb::docdb::StorageDbType::kRegular,
            ::yb::dockv::HybridTimeRequired::kFalse);
        cout << indent << indent << indent << indent << "Key: " << formatted_key << endl;
      }
      if (kv.has_value()) {
        Result<std::string> formatted_value = DocDBValueToDebugStr(
            kv.key(), ::yb::docdb::StorageDbType::kRegular, kv.value(),
            nullptr /*schema_packing_provider*/);
        cout << indent << indent << indent << indent << "Value: " << formatted_value << endl;
      }
      if (kv.has_external_hybrid_time()) {
        HybridTime ht(kv.external_hybrid_time());
        cout << indent << indent << indent << indent
             << "external_hybrid_time: " << ht.ToDebugString() << endl;
      }
      cout << indent << indent << indent << "}" << endl;  // write_pairs {
    }
    cout << indent << indent << "}" << endl;  // write_batch {
  }
  cout << indent << "}" << endl;  // write {

  return Status::OK();
}

Status PrintDecodedTransactionStatePB(const string& indent,
                                      const tablet::TransactionStatePB& update) {
  cout << indent << "update_transaction {" << endl;
  if (update.has_transaction_id()) {
    Slice txn_id_slice(update.transaction_id().c_str(), 16);
    Result<TransactionId> txn_id = FullyDecodeTransactionId(txn_id_slice);
    cout << indent << indent << "transaction_id: " << txn_id << endl;
  }
  if (update.has_status()) {
    cout << indent << indent << "status: " << TransactionStatus_Name(update.status()) << endl;
  }
  if (update.tablets_size() > 0) {
    cout << indent << indent << "tablets: ";
    for (int i = 0; i < update.tablets_size(); i++) {
      cout << update.tablets(i) << " ";
    }
    cout << endl;
  }
  if (update.tablet_batches_size() > 0) {
    cout << indent << indent << "tablet_batches: ";
    for (int i = 0; i < update.tablet_batches_size(); i++) {
      cout << update.tablet_batches(i) << " ";
    }
    cout << endl;
  }
  if (update.has_commit_hybrid_time()) {
    HybridTime ht(update.commit_hybrid_time());
    cout << indent << indent << "commit_hybrid_time: " << ht.ToDebugString() << endl;
  }
  if (update.has_sealed()) {
    cout << indent << indent << "sealed: " << update.sealed() << endl;
  }
  if (update.has_aborted() && update.aborted().set_size() > 0) {
    cout << indent << indent << "aborted: ";
    for (int i = 0; i < update.aborted().set_size(); i++) {
      cout << update.aborted().set(i) << " ";
    }
    cout << endl;
  }
  cout << indent << "}" << endl;  // update_transaction {

  return Status::OK();
}

Status PrintDecoded(const LogEntryPB& entry) {
  cout << "replicate {" << endl;
  PrintIdOnly(entry);

  const string indent = "  ";
  if (entry.has_replicate()) {
    // We can actually decode REPLICATE messages.

    const ReplicateMsg& replicate = entry.replicate();
    if (replicate.op_type() == consensus::WRITE_OP) {
      RETURN_NOT_OK(PrintDecodedWriteRequestPB(indent, replicate.write()));
    } else if (replicate.op_type() == consensus::UPDATE_TRANSACTION_OP) {
      RETURN_NOT_OK(PrintDecodedTransactionStatePB(indent, replicate.transaction_state()));
    } else {
      cout << indent << replicate.ShortDebugString() << endl;
    }
  }

  cout << "}" << endl;  // replicate {

  return Status::OK();
}

Status PrintSegment(const scoped_refptr<ReadableLogSegment>& segment) {
  PrintEntryType print_type = ParsePrintType();
  if (FLAGS_print_headers) {
    cout << "Header:\n" << segment->header().DebugString();
  }
  auto read_entries = segment->ReadEntries();
  RETURN_NOT_OK(read_entries.status);

  if (print_type == DONT_PRINT) return Status::OK();

  for (const auto& lw_entry : read_entries.entries) {
    auto entry = lw_entry->ToGoogleProtobuf();
    if (print_type == PRINT_PB) {
      if (FLAGS_truncate_data > 0) {
        pb_util::TruncateFields(&entry, FLAGS_truncate_data);
      }

      cout << "Entry:\n" << entry.DebugString();
    } else if (print_type == PRINT_DECODED) {
      RETURN_NOT_OK(PrintDecoded(entry));
    } else if (print_type == PRINT_ID) {
      PrintIdOnly(entry);
    }
  }
  if (FLAGS_print_headers && segment->HasFooter()) {
    cout << "Footer:\n" << segment->footer().DebugString();
  }

  return Status::OK();
}

Status DumpLog(const string& tablet_id, const string& tablet_wal_path) {
  Env *env = Env::Default();
  FsManagerOpts fs_opts;
  fs_opts.read_only = true;
  FsManager fs_manager(env, fs_opts);

  RETURN_NOT_OK(fs_manager.CheckAndOpenFileSystemRoots());
  std::unique_ptr<LogReader> reader;
  RETURN_NOT_OK(LogReader::Open(env,
                                scoped_refptr<LogIndex>(),
                                "Log reader: ",
                                tablet_wal_path,
                                scoped_refptr<MetricEntity>(),
                                scoped_refptr<MetricEntity>(),
                                &reader));

  SegmentSequence segments;
  RETURN_NOT_OK(reader->GetSegmentsSnapshot(&segments));

  for (const scoped_refptr<ReadableLogSegment>& segment : segments) {
    RETURN_NOT_OK(PrintSegment(segment));
  }

  return Status::OK();
}

Status DumpSegment(const string &segment_path) {
  Env *env = Env::Default();
  auto segment = VERIFY_RESULT(ReadableLogSegment::Open(env, segment_path));
  RETURN_NOT_OK(PrintSegment(segment));

  return Status::OK();
}

Status FilterLogSegment(const string& segment_path) {
  Env *const env = Env::Default();

  auto output_wal_dir = FLAGS_output_wal_dir;
  if (output_wal_dir.empty()) {
    return STATUS(InvalidArgument, "--output_wal_dir not specified");
  }

  if (env->DirExists(output_wal_dir)) {
    return STATUS_FORMAT(IllegalState, "Directory '$0' already exists", output_wal_dir);
  }
  RETURN_NOT_OK(env->CreateDir(output_wal_dir));
  output_wal_dir = VERIFY_RESULT(env->Canonicalize(output_wal_dir));
  LOG(INFO) << "Created directory " << output_wal_dir;

  auto segment = VERIFY_RESULT(ReadableLogSegment::Open(env, segment_path));
  Schema tablet_schema;
  const auto& segment_header = segment->header();

  RETURN_NOT_OK(SchemaFromPB(segment->header().deprecated_schema(), &tablet_schema));

  auto log_options = LogOptions();
  log_options.env = env;

  // We have to subtract one here because the Log implementation will add one for the new segment.
  log_options.initial_active_segment_sequence_number = segment_header.sequence_number() - 1;
  const auto source_segment_size_bytes = VERIFY_RESULT(env->GetFileSize(segment_path));
  // Set the target segment size slightly larger to make sure all the data fits. Also round it up
  // to the nearest 1 MB.
  const auto target_segment_size_bytes = (
      static_cast<size_t>(source_segment_size_bytes * 1.1) + 1_MB - 1) / 1_MB * 1_MB;
  log_options.initial_segment_size_bytes = target_segment_size_bytes;
  log_options.segment_size_bytes = target_segment_size_bytes;
  LOG(INFO) << "Source segment size " << segment_path
            << ": " << source_segment_size_bytes << " bytes";
  LOG(INFO) << "Target segment size: "
            << target_segment_size_bytes << " bytes";
  std::unique_ptr<ThreadPool> log_thread_pool;
  RETURN_NOT_OK(ThreadPoolBuilder("log").unlimited_threads().Build(&log_thread_pool));

  const OpId first_op_id_to_omit = { FLAGS_min_op_term_to_omit, FLAGS_min_op_index_to_omit };
  const auto first_op_id_to_omit_valid = first_op_id_to_omit.valid();
  if (!first_op_id_to_omit_valid && first_op_id_to_omit != OpId::Invalid()) {
    return STATUS(InvalidArgument,
                  "--min_op_term_to_omit / --min_op_index_to_omit can only be specified together");
  }

  const OpId last_op_id_to_omit = { FLAGS_max_op_term_to_omit, FLAGS_max_op_index_to_omit };
  const auto last_op_id_to_omit_valid = last_op_id_to_omit.valid();
  if (!last_op_id_to_omit_valid && last_op_id_to_omit != OpId::Invalid()) {
    return STATUS(InvalidArgument,
                  "--max_op_term_to_omit / --max_op_index_to_omit can only be specified together");
  }

  // If neither first/last OpId to omit are specified, we will just copy all operations to the
  // output file. This might be useful in some kinds of testing or troubleshooting.
  const bool omit_something = first_op_id_to_omit_valid || last_op_id_to_omit_valid;

  // Invalid first/last OpId to omit indicate an open range of OpIds to omit.
  const bool omit_starting_with_earliest_op_id = omit_something && !first_op_id_to_omit_valid;
  const bool omit_to_infinite_op_id = omit_something && !last_op_id_to_omit_valid;

  if (omit_something) {
    if (first_op_id_to_omit_valid && last_op_id_to_omit_valid) {
      LOG(INFO) << "Will omit records between OpIds " << first_op_id_to_omit << " and "
                << last_op_id_to_omit << " (including the exact OpId matches).";
    } else if (first_op_id_to_omit_valid) {
      LOG(INFO) << "Will omit records with OpId greater than or equal to " << first_op_id_to_omit;
    } else {
      LOG(INFO) << "Will omit records with OpId less than or equal to " << last_op_id_to_omit;
    }
  } else {
    LOG(INFO) << "Will include all records of the source WAL in the output";
  }

  scoped_refptr<Log> log;
  RETURN_NOT_OK(Log::Open(
      log_options,
      segment_header.unused_tablet_id(),
      output_wal_dir,
      "log-dump-tool",
      tablet_schema,
      segment_header.deprecated_schema_version(),
      /* table_metric_entity */ nullptr,
      /* tablet_metric_entity */ nullptr,
      log_thread_pool.get(),
      log_thread_pool.get(),
      log_thread_pool.get(),
      /* cdc_min_replicated_index */ 0,
      &log));

  auto read_entries = segment->ReadEntries();
  RETURN_NOT_OK(read_entries.status);
  uint64_t num_omitted = 0;
  uint64_t num_included = 0;
  CHECK_EQ(read_entries.entries.size(), read_entries.entry_metadata.size());
  for (size_t i = 0; i < read_entries.entries.size(); ++i) {
    auto& entry_ptr = read_entries.entries[i];
    const OpId op_id = OpId::FromPB(entry_ptr->replicate().id());
    if (omit_something &&
        (omit_starting_with_earliest_op_id ||
         (first_op_id_to_omit_valid && op_id >= first_op_id_to_omit)) &&
        (omit_to_infinite_op_id ||
         (last_op_id_to_omit_valid && op_id <= last_op_id_to_omit))) {
      num_omitted++;
      continue;
    }
    RETURN_NOT_OK(log->Append(entry_ptr, read_entries.entry_metadata[i], SkipWalWrite::kFalse));
    num_included++;
  }
  LOG(INFO) << "Included " << num_included << " entries, omitted " << num_omitted << " entries";
  RETURN_NOT_OK(log->Close());

  auto resulting_files = VERIFY_RESULT(
      env->GetChildren(output_wal_dir, ExcludeDots::kTrue));
  sort(resulting_files.begin(), resulting_files.end());
  for (const auto& resulting_file_name : resulting_files) {
    LOG(INFO) << "Generated file " << JoinPathSegments(output_wal_dir, resulting_file_name);
  }

  return Status::OK();
}

}  // namespace log
}  // namespace yb

int main(int argc, char **argv) {
  yb::ParseCommandLineFlags(&argc, &argv, true);
  using yb::Status;

  if (argc != 2 && argc != 3) {
    std::cerr << "usage: " << argv[0]
              << " --fs_data_dirs <dirs>"
              << " {<tablet_name> <log path>} | <log segment path>"
              << " [--filter_log_segment --output_wal_dir <dest_dir>]"
              << std::endl;
    return 1;
  }

  yb::Status status;
  yb::InitGoogleLoggingSafeBasic(argv[0]);
  yb::HybridTime::TEST_SetPrettyToString(true);
  if (argc == 2) {
    if (FLAGS_filter_log_segment) {
      status = yb::log::FilterLogSegment(argv[1]);
    } else {
      status = yb::log::DumpSegment(argv[1]);
    }
  } else {
    if (FLAGS_filter_log_segment) {
      status = STATUS(
          InvalidArgument,
          "--filter_log_segment is only allowed when a single segment file is specified");
    } else {
      status = yb::log::DumpLog(argv[1], argv[2]);
    }
  }

  if (status.ok()) {
    return 0;
  }
  std::cerr << "Error: " << status.ToString() << std::endl;
  return 1;
}
