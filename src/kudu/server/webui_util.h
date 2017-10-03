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
#ifndef KUDU_SERVER_WEBUI_UTIL_H
#define KUDU_SERVER_WEBUI_UTIL_H

#include <string>
#include <sstream>
#include <vector>

#include "kudu/gutil/ref_counted.h"

namespace kudu {

class Schema;
class MonitoredTask;

void HtmlOutputSchemaTable(const Schema& schema,
                           std::stringstream* output);
void HtmlOutputImpalaSchema(const std::string& table_name,
                            const Schema& schema,
                            const std::string& master_address,
                            std::stringstream* output);
void HtmlOutputTaskList(const std::vector<scoped_refptr<MonitoredTask> >& tasks,
                        std::stringstream* output);
} // namespace kudu

#endif // KUDU_SERVER_WEBUI_UTIL_H
