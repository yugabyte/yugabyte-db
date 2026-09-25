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

  // Prints the usage message prefixed with the program name, as gflags does in --help* headers.
  static void PrintOverview(const std::string& prog_name, std::ostream& out);

 protected:
  typedef std::function<Status(const CLIArguments&, ClusterAdminClient* client)> Action;

  struct Command {
    std::string name_;
    std::string usage_arguments_;
    Action action_;
    bool hidden_;
  };

  void Register(
      std::string&& cmd_name, const std::string& cmd_args, Action&& action, bool hidden = false);
  void SetUsage();

  virtual void RegisterCommandHandlers();

 private:
  struct HelpRequest {
    // The `help` operation, with its arguments in help_args, rather than a --help* flag.
    bool help_operation = false;
    std::vector<std::string> help_args;
    // For a --help* flag: the operation to show usage for, or empty for the overview.
    std::string operation;
    bool helpshort = false;
  };

  // Scans raw argv for --help/-h/--helpshort or a leading `help` operation, so help answers even
  // when the flag parse would fail. Returns nullopt when help was not requested.
  std::optional<HelpRequest> ScanForHelpRequest(int argc, char** argv) const;
  void PrintHelpRequest(const HelpRequest& request, std::ostream& out);
  Status RunHelp(const CLIArguments& args);
  void PrintCommandUsage(const Command& command, std::ostream& out);
  // Prints the visible operations whose name contains filter (case-insensitive), numbered when
  // filter is empty. Returns the number printed.
  size_t PrintOperationNames(std::ostream& out, const std::string& filter = "") const;
  void ReportUnknownOperation(const std::string& op) const;
  Status RunCommand(const Command& command, const CLIArguments& command_args);
  std::string GetArgumentExpressions(const std::string& usage_arguments);
  // Returns the command names to suggest for an operation that did not match any registered
  // command, or an empty vector when there is no good suggestion. Commands that the operation is a
  // prefix of are preferred, then the closest commands by edit distance, then token matches.
  std::vector<std::string> GetSuggestedCommands(const std::string& op) const;
  std::vector<Command> commands_;
  std::map<std::string, size_t> command_indexes_;
  std::unique_ptr<ClusterAdminClient> client_;
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
