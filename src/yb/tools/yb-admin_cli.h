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
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include <functional>
#include <iosfwd>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <rapidjson/document.h>

#include "yb/util/status_fwd.h"
#include "yb/tools/tools_fwd.h"

namespace yb {
namespace client {

class YBTableName;

} // namespace client

namespace tools {

// Tool to administer a cluster from the CLI.
class ClusterAdminCli {
 public:
  typedef std::vector<std::string> CLIArguments;

  virtual ~ClusterAdminCli() = default;

  Status Run(int argc, char** argv);

  static const Status kInvalidArguments;

  // Returned by an action that has already written its complete error report to stderr.
  // RunCommand() adds nothing on top of it; main() still exits non-zero.
  static const Status kErrorReported;

  // Writes the short overview: the usage message prefixed with the program name, exactly as
  // gflags renders it in the --help* headers.
  static void PrintOverview(const std::string& prog_name, std::ostream& out);

 protected:
  typedef std::function<Status(const CLIArguments&, ClusterAdminClient* client)> Action;

  struct Command {
    std::string name_;
    std::string usage_arguments_;
    Action action_;
    bool hidden_;
    // Whether Run() must construct and connect a ClusterAdminClient before dispatching to
    // action_. False for operations like `help` that must work with no reachable cluster.
    bool needs_client_ = true;
  };

  void Register(
      std::string&& cmd_name, const std::string& cmd_args, Action&& action, bool hidden = false,
      bool needs_client = true);
  void SetUsage(const std::string& prog_name);

  virtual void RegisterCommandHandlers();

 private:
  // A help request found in raw argv before the flag parse (see ScanForHelpRequest).
  struct HelpRequest {
    // Set when the request is the `help` operation rather than a --help* flag. Run() then
    // dispatches it through RunCommand() so that answering it early changes nothing about its
    // output or its error framing, and help_args holds the operation's own arguments.
    bool help_operation = false;
    std::vector<std::string> help_args;
    // First token that names a registered operation, or empty for the plain overview. Unused for
    // a help_operation request, which carries its target in help_args.
    std::string operation;
    // Whether --helpshort was requested, adding yb-admin's own flags to the overview.
    bool helpshort = false;
  };

  // Scans raw argv for --help/-h/--helpshort and for a leading `help` operation before the flag
  // parse, so every help surface answers even when the parse would fail on a malformed
  // --flagfile, an unparsable flag value, or an unknown flag. Returns nullopt when help was not
  // requested.
  std::optional<HelpRequest> ScanForHelpRequest(int argc, char** argv) const;
  void PrintHelpRequest(const HelpRequest& request, const std::string& prog_name,
                        std::ostream& out);
  // Prints "Usage: <prog> <operation> <args>" plus the argument-placeholder definitions -- the
  // one code path behind `help <operation>`, `<operation> --help`, and the bad-argument error.
  void PrintCommandUsage(const Command& command, const std::string& prog_name, std::ostream& out);
  // Prints the visible operations alphabetically -- numbered when filter is empty, or the
  // case-insensitive substring matches when it is not. Returns how many entries were printed;
  // prints nothing (not even a header) when a non-empty filter matches nothing.
  size_t PrintOperationNames(std::ostream& out, const std::string& filter = "") const;
  // Prints "Invalid operation: <op>" with closest-match suggestions to stderr.
  void ReportUnknownOperation(const std::string& op, const std::string& prog_name) const;
  Status RunCommand(
      const Command& command, const CLIArguments& command_args, const std::string& program_name);
  std::string GetArgumentExpressions(const std::string& usage_arguments);
  // Returns the command names to suggest for an operation that did not match any registered
  // command, or an empty vector when there is no good suggestion. Commands that the operation is a
  // prefix of are preferred; otherwise the closest commands by edit distance are returned.
  std::vector<std::string> GetSuggestedCommands(const std::string& op) const;
  std::vector<Command> commands_;
  std::map<std::string, size_t> command_indexes_;
  std::unique_ptr<ClusterAdminClient> client_;
  // BaseName(argv[0]), set at the top of Run(); actions (the `help` lambda) need it and only
  // receive (args, client).
  std::string prog_name_;
};

using CLIArgumentsIterator = ClusterAdminCli::CLIArguments::const_iterator;
using TailArgumentsProcessor =
    std::function<Status(CLIArgumentsIterator, const CLIArgumentsIterator&)>;

Result<std::vector<client::YBTableName>> ResolveTableNames(
    ClusterAdminClient* client,
    CLIArgumentsIterator i,
    const CLIArgumentsIterator& end,
    const TailArgumentsProcessor& tail_processor = TailArgumentsProcessor(),
    bool allow_namespace_only = false);

Result<client::YBTableName> ResolveSingleTableName(
    ClusterAdminClient* client,
    CLIArgumentsIterator i,
    const CLIArgumentsIterator& end,
    TailArgumentsProcessor tail_processor = TailArgumentsProcessor());

Status CheckArgumentsCount(size_t count, size_t min, size_t max);

}  // namespace tools
}  // namespace yb
