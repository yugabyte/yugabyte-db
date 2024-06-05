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

#include "yb/server/webui_util.h"

#include <string>

#include "yb/common/schema.h"

#include "yb/gutil/strings/human_readable.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/server/monitored_task.h"

#include "yb/util/url-coding.h"

using strings::Substitute;

namespace yb {
namespace server {

void HtmlOutputSchemaTable(const Schema& schema,
                           std::stringstream* output) {
  *output << "<table class='table table-striped'>\n";
  *output << "<tr><th>Column</th><th>ID</th><th>Type</th></tr>\n";

  for (size_t i = 0; i < schema.num_columns(); i++) {
    const ColumnSchema& col = schema.column(i);
    *output << Format("<tr><th>$0</th><td>$1</td><td>$2</td></tr>\n",
                      EscapeForHtmlToString(col.name()),
                      schema.column_id(i),
                      col.TypeToString());
  }
  *output << "</table>\n";
}

void HtmlOutputTask(const std::shared_ptr<MonitoredTask>& task,
                    std::stringstream* output) {
  double time_since_started = 0;
  if (task->start_timestamp().Initialized()) {
    time_since_started =
        MonoTime::Now().GetDeltaSince(task->start_timestamp()).ToSeconds();
  }
  double running_secs = 0;
  if (task->completion_timestamp().Initialized()) {
    running_secs = task->completion_timestamp().GetDeltaSince(
        task->start_timestamp()).ToSeconds();
  } else if (task->start_timestamp().Initialized()) {
    running_secs = MonoTime::Now().GetDeltaSince(
        task->start_timestamp()).ToSeconds();
  }

  *output << Substitute(
      "<tr><th>$0</th><td>$1</td><td>$2 ago</td><td>$3</td><td>$4</td></tr>\n",
      EscapeForHtmlToString(task->type_name()),
      EscapeForHtmlToString(ToString(task->state())),
      EscapeForHtmlToString(
          HumanReadableElapsedTime::ToShortString(time_since_started)),
      EscapeForHtmlToString(
          HumanReadableElapsedTime::ToShortString(running_secs)),
      EscapeForHtmlToString(task->description()));
}

void HtmlOutputTasks(const std::unordered_set<std::shared_ptr<MonitoredTask>>& tasks,
                        std::stringstream* output) {
  *output << "<table class='table table-striped'>\n";
  *output << "  <tr><th>Task Name</th><th>State</th><th>Start "
             "Time</th><th>Duration</th><th>Description</th></tr>\n";
  for (const auto& task : tasks) {
    HtmlOutputTask(task, output);
  }
  *output << "</table>\n";
}

} // namespace server
} // namespace yb
