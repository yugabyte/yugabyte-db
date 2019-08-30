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

#include <iostream>
#include <vector>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "yb/common/wire_protocol.h"
#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/log_index.h"
#include "yb/consensus/log_reader.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/numbers.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/util/env.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/pb_util.h"

DEFINE_bool(print_headers, true, "print the log segment headers/footers");
DEFINE_string(print_entries, "decoded",
              "How to print entries:\n"
              "  false|0|no = don't print\n"
              "  true|1|yes|decoded = print them decoded\n"
              "  pb = print the raw protobuf\n"
              "  id = print only their ids");
DEFINE_int32(truncate_data, 100,
             "Truncate the data fields to the given number of bytes "
             "before printing. Set to 0 to disable");
namespace yb {
namespace log {

using consensus::OperationType;
using consensus::ReplicateMsg;
using std::string;
using std::vector;
using std::cout;
using std::endl;
using tablet::RaftGroupMetadata;
using tserver::WriteRequestPB;

enum PrintEntryType {
  DONT_PRINT,
  PRINT_PB,
  PRINT_DECODED,
  PRINT_ID
};

static PrintEntryType ParsePrintType() {
  if (ParseLeadingBoolValue(FLAGS_print_entries.c_str(), true) == false) {
    return DONT_PRINT;
  } else if (ParseLeadingBoolValue(FLAGS_print_entries.c_str(), false) == true ||
             FLAGS_print_entries == "decoded") {
    return PRINT_DECODED;
  } else if (FLAGS_print_entries == "pb") {
    return PRINT_PB;
  } else if (FLAGS_print_entries == "id") {
    return PRINT_ID;
  } else {
    LOG(FATAL) << "Unknown value for --print_entries: " << FLAGS_print_entries;
  }
}

void PrintIdOnly(const LogEntryPB& entry) {
  switch (entry.type()) {
    case log::REPLICATE:
    {
      cout << entry.replicate().id().term() << "." << entry.replicate().id().index()
           << "@" << entry.replicate().hybrid_time() << "\t";
      cout << "REPLICATE "
           << OperationType_Name(entry.replicate().op_type());
      break;
    }
    default:
      cout << "UNKNOWN: " << entry.ShortDebugString();
  }

  cout << endl;
}

Status PrintDecodedWriteRequestPB(const string& indent,
                                  const Schema& tablet_schema,
                                  const WriteRequestPB& write) {
  Arena arena(32 * 1024, 1024 * 1024);

  cout << indent << "Tablet: " << write.tablet_id() << endl;
  if (write.has_propagated_hybrid_time()) {
    cout << indent << "Propagated TS: " << write.propagated_hybrid_time() << endl;
  }

  return Status::OK();
}

Status PrintDecoded(const LogEntryPB& entry, const Schema& tablet_schema) {
  PrintIdOnly(entry);

  const string indent = "\t";
  if (entry.has_replicate()) {
    // We can actually decode REPLICATE messages.

    const ReplicateMsg& replicate = entry.replicate();
    if (replicate.op_type() == consensus::WRITE_OP) {
      RETURN_NOT_OK(PrintDecodedWriteRequestPB(indent, tablet_schema, replicate.write_request()));
    } else {
      cout << indent << replicate.ShortDebugString() << endl;
    }
  }

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

  Schema tablet_schema;
  RETURN_NOT_OK(SchemaFromPB(segment->header().schema(), &tablet_schema));

  for (const auto& entry : read_entries.entries) {

    if (print_type == PRINT_PB) {
      if (FLAGS_truncate_data > 0) {
        pb_util::TruncateFields(entry.get(), FLAGS_truncate_data);
      }

      cout << "Entry:\n" << entry->DebugString();
    } else if (print_type == PRINT_DECODED) {
      RETURN_NOT_OK(PrintDecoded(*entry, tablet_schema));
    } else if (print_type == PRINT_ID) {
      PrintIdOnly(*entry);
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

  RETURN_NOT_OK(fs_manager.Open());
  std::unique_ptr<LogReader> reader;
  RETURN_NOT_OK(LogReader::Open(env,
                                scoped_refptr<LogIndex>(),
                                tablet_id,
                                tablet_wal_path,
                                fs_manager.uuid(),
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
  scoped_refptr<ReadableLogSegment> segment;
  RETURN_NOT_OK(ReadableLogSegment::Open(env, segment_path, &segment));
  RETURN_NOT_OK(PrintSegment(segment));

  return Status::OK();
}

} // namespace log
} // namespace yb

int main(int argc, char **argv) {
  yb::ParseCommandLineFlags(&argc, &argv, true);
  if (argc != 2 && argc != 3) {
    std::cerr << "usage: " << argv[0]
              << " -fs_data_dirs <dirs>"
              << " {<tablet_name> <log path>} | <log segment path>"
              << std::endl;
    return 1;
  }

  yb::Status status;
  if (argc == 2) {
    yb::InitGoogleLoggingSafe(argv[0]);
    status = yb::log::DumpSegment(argv[1]);
    if (status.ok()) {
      return 0;
    }
  } else {
    status = yb::log::DumpLog(argv[1], argv[2]);
    if (status.ok()) {
      return 0;
    }
  }
  std::cerr << "Error: " << status.ToString() << std::endl;
  return 1;
}
