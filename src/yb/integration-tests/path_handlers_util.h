// Copyright (c) YugabyteDB, Inc.
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
#include <vector>

#include "yb/util/result.h"

namespace yb::integration_tests::path_handlers_util {

Status GetUrl(
    const std::string& url, faststring* result,
    MonoDelta timeout = MonoDelta::FromSeconds(30));

Result<std::vector<std::vector<std::string>>> GetHtmlTableRows(
    const std::string& url, const std::string& html_table_tag_id, bool include_header = false);

Result<std::vector<std::string>> GetHtmlTableColumn(
    const std::string& url, const std::string& html_table_tag_id,
    const std::string& column_header);

} // namespace yb::integration_tests::path_handlers_util
