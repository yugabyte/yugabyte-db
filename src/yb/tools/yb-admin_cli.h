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
#ifndef YB_TOOLS_YB_ADMIN_CLI_H
#define YB_TOOLS_YB_ADMIN_CLI_H

#include <functional>
#include <map>
#include <vector>
#include <string>

#include <rapidjson/document.h>

#include "yb/util/status_fwd.h"
#include "yb/tools/tools_fwd.h"

namespace yb {
namespace client {

class YBTableName;

} // namespace client

namespace tools {

typedef enterprise::ClusterAdminClient ClusterAdminClientClass;

// Tool to administer a cluster from the CLI.
class ClusterAdminCli {
 public:
  typedef std::vector<std::string> CLIArguments;

  virtual ~ClusterAdminCli() = default;

  Status Run(int argc, char** argv);

  static const Status kInvalidArguments;

 protected:
  typedef std::function<Status(const CLIArguments&)> Action;
  typedef std::function<Result<rapidjson::Document>(const CLIArguments&)> JsonAction;

  struct Command {
    std::string name_;
    std::string usage_arguments_;
    Action action_;
  };

  void Register(std::string&& cmd_name, std::string&& cmd_args, Action&& action);
  void RegisterJson(std::string&& cmd_name, std::string&& cmd_args, JsonAction&& action);
  void SetUsage(const std::string& prog_name);

  virtual void RegisterCommandHandlers(ClusterAdminClientClass* client);

 private:
  Status RunCommand(
      const Command& command, const CLIArguments& command_args, const std::string& program_name);
  std::vector<Command> commands_;
  std::map<std::string, size_t> command_indexes_;
};

using CLIArgumentsIterator = ClusterAdminCli::CLIArguments::const_iterator;
using TailArgumentsProcessor =
    std::function<Status(CLIArgumentsIterator, const CLIArgumentsIterator&)>;

Result<std::vector<client::YBTableName>> ResolveTableNames(
    ClusterAdminClientClass* client,
    CLIArgumentsIterator i,
    const CLIArgumentsIterator& end,
    const TailArgumentsProcessor& tail_processor = TailArgumentsProcessor(),
    bool allow_namespace_only = false);

Result<client::YBTableName> ResolveSingleTableName(
    ClusterAdminClientClass* client,
    CLIArgumentsIterator i,
    const CLIArgumentsIterator& end,
    TailArgumentsProcessor tail_processor = TailArgumentsProcessor());

Status CheckArgumentsCount(size_t count, size_t min, size_t max);

}  // namespace tools
}  // namespace yb

#endif // YB_TOOLS_YB_ADMIN_CLI_H
