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
// Tool to list local files and directories

#include "yb/tools/fs_tool.h"

#include <iostream>
#include <vector>

#include "yb/util/logging.h"

#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/status.h"
#include "yb/util/status_log.h"

DEFINE_NON_RUNTIME_bool(verbose, false, "Print additional information (e.g., log segment headers)");

namespace yb {
namespace tools {

using std::string;
using std::vector;

namespace {

enum CommandType {
  FS_TREE = 1,
  LIST_LOGS = 2,
  LIST_TABLETS = 3,
};

// TODO: extract and generalized the "verb" handling code with other
// tools such that it can be shared with other tools.

struct CommandHandler {
  CommandType type_;
  string name_;
  string desc_;

  CommandHandler(CommandType type, string name, string desc)
      : type_(type), name_(std::move(name)), desc_(std::move(desc)) {}
};

const vector<CommandHandler> kCommandHandlers = {
    CommandHandler(FS_TREE, "tree", "Print out a file system tree." ),
    CommandHandler(LIST_LOGS, "list_logs",
                   "List file system logs (optionally accepts a tablet id)."),
    CommandHandler(LIST_TABLETS, "list_tablets", "List tablets." ) };

void PrintUsageToStream(const string& prog_name, std::ostream* out) {
  *out << "Usage: " << prog_name << " [-verbose] "
       << "-fs_wal_dirs <dirs> -fs_data_dirs <dirs> <command> [option] "
       << std::endl << std::endl
       << "Commands: " << std::endl;
  for (const CommandHandler& handler : kCommandHandlers) {
    *out << handler.name_ << ": " << handler.desc_ << std::endl;
  }
}

void Usage(const string& prog_name, const string& msg) {
  std::cerr << "Error " << prog_name << ": " << msg << std::endl
            << std::endl;
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
  Usage("Invalid command specified ", argv[1]);
  return false;
}

} // anonymous namespace

static int FsListToolMain(int argc, char** argv) {
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

  FsTool fs_tool(FLAGS_verbose ? FsTool::HEADERS_ONLY : FsTool::MINIMUM);
  CHECK_OK_PREPEND(fs_tool.Init(), "Error initializing file system tool");

  switch (cmd) {
    case FS_TREE: {
      CHECK_OK(fs_tool.FsTree());
      break;
    }
    case LIST_LOGS: {
      if (argc > 2) {
        CHECK_OK(fs_tool.ListLogSegmentsForTablet(argv[2]));
      } else {
        CHECK_OK(fs_tool.ListAllLogSegments());
      }
      break;
    }
    case LIST_TABLETS: {
      CHECK_OK(fs_tool.ListAllTablets());
      break;
    }
  }

  return 0;
}

} // namespace tools
} // namespace yb

int main(int argc, char** argv) {
  return yb::tools::FsListToolMain(argc, argv);
}
