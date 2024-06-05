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
// Tool to dump tablets
#include <iostream>
#include <memory>
#include <vector>

#include "yb/tools/fs_tool.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/status.h"
#include "yb/util/status_log.h"

DEFINE_NON_RUNTIME_int32(nrows, 0, "Number of rows to dump");
DEFINE_NON_RUNTIME_bool(metadata_only, false, "Whether just to dump the block metadata, "
                                  "when printing blocks.");

/*
  TODO: support specifying start and end keys

  DEFINE_NON_RUNTIME_string(start_key, "", "Start key for rows to dump");
  DEFINE_NON_RUNTIME_string(end_key, "", "Start key for rows to dump");
*/

DEFINE_NON_RUNTIME_bool(headers_only, false, "Don't dump contents, dump headers only");

namespace yb {
namespace tools {

using std::string;
using std::vector;
using strings::Substitute;

namespace {

enum CommandType {
  DUMP_TABLET_DATA,
  PRINT_TABLET_META,
  PRINT_UUID,
};

struct CommandHandler {
  CommandType type_;
  string name_;
  string desc_;

  CommandHandler(CommandType type, string name, string desc)
      : type_(type), name_(std::move(name)), desc_(std::move(desc)) {}
};

const vector<CommandHandler> kCommandHandlers = {
    CommandHandler(DUMP_TABLET_DATA, "dump_tablet_data",
                   "Dump a tablet's data (requires a tablet id)"),
    CommandHandler(PRINT_TABLET_META, "print_meta",
                   "Print a tablet metadata (requires a tablet id)"),
    CommandHandler(PRINT_UUID, "print_uuid",
                   "Print the UUID (master or TS) to whom the data belongs") };

void PrintUsageToStream(const std::string& prog_name, std::ostream* out) {
  *out << "Usage: " << prog_name
       << " [-headers_only] [-nrows <num rows>] "
       << "-fs_wal_dirs <dirs> -fs_data_dirs <dirs> <command> <options> "
       << std::endl << std::endl;
  *out << "Commands: " << std::endl;
  for (const CommandHandler& handler : kCommandHandlers) {
    *out << handler.name_ << ": " << handler.desc_ << std::endl;
  }
}
void Usage(const string& prog_name, const string& msg) {
  std::cerr << "Error " << prog_name << ": " << msg << std::endl;
  PrintUsageToStream(prog_name, &std::cerr);
}

bool ValidateCommand(int argc, char** argv, CommandType* out) {
  if (argc < 2) {
    Usage(argv[0], "At least one command must be specified!");
    return false;
  }
  for (const CommandHandler& handler : kCommandHandlers) {
    if (argv[1] == handler.name_) {
      *out = handler.type_;
      return true;
    }
  }
  Usage("Invalid command specified: ", argv[1]);
  return false;
}

} // anonymous namespace

static int FsDumpToolMain(int argc, char** argv) {
  FLAGS_logtostderr = 1;
  std::stringstream usage_str;
  PrintUsageToStream(argv[0], &usage_str);
  google::SetUsageMessage(usage_str.str());
  ParseCommandLineFlags(&argc, &argv, true);
  InitGoogleLoggingSafe(argv[0]);

  CommandType cmd;
  if (!ValidateCommand(argc, argv, &cmd)) {
    return 2;
  }

  FsTool fs_tool(FLAGS_headers_only ? FsTool::HEADERS_ONLY : FsTool::MAXIMUM);
  CHECK_OK(fs_tool.Init());

  DumpOptions opts;
  // opts.start_key = FLAGS_start_key;
  // opts.end_key = FLAGS_end_key;
  opts.nrows = FLAGS_nrows;
  opts.metadata_only = FLAGS_metadata_only;

  switch (cmd) {
    case DUMP_TABLET_DATA:
    {
      if (argc < 3) {
        Usage(argv[0],
              Substitute("dump_tablet requires tablet id: $0 "
                         "dump_tablet <tablet_id>",
                         argv[0]));
        return 2;
      }
      CHECK_OK(fs_tool.DumpTabletData(argv[2]));
      break;
    }

    case PRINT_TABLET_META: {
      if (argc < 3) {
        Usage(argv[0], Substitute("print_meta requires a tablet id: $0"
                                  "print_meta <tablet_id>", argv[0]));
        return 2;
      }
      CHECK_OK(fs_tool.PrintTabletMeta(argv[2], 0));
      break;
    }
    case PRINT_UUID: {
      if (argc < 2) {
        Usage(argv[0], Substitute("$0 print_uuid", argv[0]));
        return 2;
      }
      CHECK_OK(fs_tool.PrintUUID(0));
      break;
    }
  }

  return 0;
}

} // namespace tools
} // namespace yb

int main(int argc, char** argv) {
  return yb::tools::FsDumpToolMain(argc, argv);
}
