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

#include "yb/util/result.h"
#include "yb/util/status.h"

namespace yb {
namespace client {

class YBTableName;

} // namespace client

namespace tools {

class ClusterAdminClient;
typedef enterprise::ClusterAdminClient ClusterAdminClientClass;

// Tool to administer a cluster from the CLI.
class ClusterAdminCli {
 public:
  typedef std::vector<std::string> CLIArguments;

  virtual ~ClusterAdminCli() = default;

  Status Run(int argc, char** argv);

  static const Status kInvalidArguments;

 protected:
  typedef std::function<Status(const CLIArguments&)> CommandFn;
  struct Command {
    std::string name_;
    std::string usage_arguments_;
    CommandFn fn_;
  };

  void Register(std::string&& cmd_name, std::string&& cmd_args, CommandFn&& cmd_fn);
  void SetUsage(const std::string& prog_name);

  virtual void RegisterCommandHandlers(ClusterAdminClientClass* client);

 private:
  std::vector<Command> commands_;
  std::map<std::string, size_t> command_indexes_;
};

Result<client::YBTableName> ResolveTableName(ClusterAdminClientClass* client,
                                             const string& full_namespace_name,
                                             const string& table_name);

}  // namespace tools
}  // namespace yb

#endif // YB_TOOLS_YB_ADMIN_CLI_H
