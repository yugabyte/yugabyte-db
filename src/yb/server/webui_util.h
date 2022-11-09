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
#pragma once

#include <string>
#include <unordered_set>

#include "yb/common/common_fwd.h"

#include "yb/server/server_fwd.h"

namespace yb {
namespace server {

void HtmlOutputSchemaTable(const Schema& schema,
                           std::stringstream* output);
void HtmlOutputTask(const std::shared_ptr<MonitoredTask>& task,
                    std::stringstream* output);
void HtmlOutputTasks(const std::unordered_set<std::shared_ptr<MonitoredTask> >& tasks,
                     std::stringstream* output);

inline std::string TableLongName(const std::string& keyspace_name,
                                 const std::string& table_name) {
  return keyspace_name + (keyspace_name.empty() ? "" : ".") + table_name;
}

} // namespace server
} // namespace yb
