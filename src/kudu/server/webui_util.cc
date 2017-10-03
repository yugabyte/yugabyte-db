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

#include "kudu/server/webui_util.h"

#include <string>

#include "kudu/common/schema.h"
#include "kudu/gutil/strings/join.h"
#include "kudu/gutil/map-util.h"
#include "kudu/gutil/strings/human_readable.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/server/monitored_task.h"
#include "kudu/util/url-coding.h"

using strings::Substitute;

namespace kudu {

void HtmlOutputSchemaTable(const Schema& schema,
                           std::stringstream* output) {
  *output << "<table class='table table-striped'>\n";
  *output << "  <tr>"
          << "<th>Column</th><th>ID</th><th>Type</th>"
          << "<th>Read default</th><th>Write default</th>"
          << "</tr>\n";

  for (int i = 0; i < schema.num_columns(); i++) {
    const ColumnSchema& col = schema.column(i);
    string read_default = "-";
    if (col.has_read_default()) {
      read_default = col.Stringify(col.read_default_value());
    }
    string write_default = "-";
    if (col.has_write_default()) {
      write_default = col.Stringify(col.write_default_value());
    }
    *output << Substitute("<tr><th>$0</th><td>$1</td><td>$2</td><td>$3</td><td>$4</td></tr>\n",
                          EscapeForHtmlToString(col.name()),
                          schema.column_id(i),
                          col.TypeToString(),
                          EscapeForHtmlToString(read_default),
                          EscapeForHtmlToString(write_default));
  }
  *output << "</table>\n";
}

void HtmlOutputImpalaSchema(const std::string& table_name,
                            const Schema& schema,
                            const string& master_addresses,
                            std::stringstream* output) {
  *output << "<code><pre>\n";

  // Escape table and column names with ` to avoid conflicts with Impala reserved words.
  *output << "CREATE EXTERNAL TABLE " << EscapeForHtmlToString("`" + table_name + "`")
          << " (\n";

  vector<string> key_columns;

  for (int i = 0; i < schema.num_columns(); i++) {
    const ColumnSchema& col = schema.column(i);

    *output << EscapeForHtmlToString("`" + col.name() + "`") << " ";
    switch (col.type_info()->type()) {
      case STRING:
        *output << "STRING";
        break;
      case BINARY:
        *output << "BINARY";
        break;
      case UINT8:
      case INT8:
        *output << "TINYINT";
        break;
      case UINT16:
      case INT16:
        *output << "SMALLINT";
        break;
      case UINT32:
      case INT32:
        *output << "INT";
        break;
      case UINT64:
      case INT64:
        *output << "BIGINT";
        break;
      case TIMESTAMP:
        *output << "TIMESTAMP";
        break;
      case FLOAT:
        *output << "FLOAT";
        break;
      case DOUBLE:
        *output << "DOUBLE";
        break;
      default:
        *output << "[unsupported type " << col.type_info()->name() << "!]";
        break;
    }
    if (i < schema.num_columns() - 1) {
      *output << ",";
    }
    *output << "\n";

    if (schema.is_key_column(i)) {
      key_columns.push_back(col.name());
    }
  }
  *output << ")\n";

  *output << "TBLPROPERTIES(\n";
  *output << "  'storage_handler' = 'com.cloudera.kudu.hive.KuduStorageHandler',\n";
  *output << "  'kudu.table_name' = '" << table_name << "',\n";
  *output << "  'kudu.master_addresses' = '" << master_addresses << "',\n";
  *output << "  'kudu.key_columns' = '" << JoinElements(key_columns, ", ") << "'\n";
  *output << ");\n";
  *output << "</pre></code>\n";
}

void HtmlOutputTaskList(const std::vector<scoped_refptr<MonitoredTask> >& tasks,
                        std::stringstream* output) {
  *output << "<table class='table table-striped'>\n";
  *output << "  <tr><th>Task Name</th><th>State</th><th>Time</th><th>Description</th></tr>\n";
  for (const scoped_refptr<MonitoredTask>& task : tasks) {
    string state;
    switch (task->state()) {
      case MonitoredTask::kStatePreparing:
        state = "Preparing";
        break;
      case MonitoredTask::kStateRunning:
        state = "Running";
        break;
      case MonitoredTask::kStateComplete:
        state = "Complete";
        break;
      case MonitoredTask::kStateFailed:
        state = "Failed";
        break;
      case MonitoredTask::kStateAborted:
        state = "Aborted";
        break;
    }

    double running_secs = 0;
    if (task->completion_timestamp().Initialized()) {
      running_secs = task->completion_timestamp().GetDeltaSince(
        task->start_timestamp()).ToSeconds();
    } else if (task->start_timestamp().Initialized()) {
      running_secs = MonoTime::Now(MonoTime::FINE).GetDeltaSince(
        task->start_timestamp()).ToSeconds();
    }

    *output << Substitute(
        "<tr><th>$0</th><td>$1</td><td>$2</td><td>$3</td></tr>\n",
        EscapeForHtmlToString(task->type_name()),
        EscapeForHtmlToString(state),
        EscapeForHtmlToString(HumanReadableElapsedTime::ToShortString(running_secs)),
        EscapeForHtmlToString(task->description()));
  }
  *output << "</table>\n";
}

} // namespace kudu
