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

#include <string>

#include "yb/util/tostring.h"

// Printing Tables which can be sorted and filtered.
// The filter box is displayed just above the table.
// Clicking on any column name will sort the table on that column value. The header row will display
// the sort direction using the symbol ▲ to indicate descending and ▼ to indicate ascending. All non
// numerical columns (isNaN) are sorted as lowercase strings.
//
// Usage:
// 1. Print the table header using HTML_PRINT_TABLE_WITH_HEADER_ROW(table_type, column_names...). Or
// HTML_PRINT_TABLE_WITH_HEADER_ROW_WITH_ID(table_type, type_id, ...) when multiple tables have same
// type (this happens when this is called inside loops).
// 2. Print each row using HTML_PRINT_TABLE_ROW(column_values...)
// 3. End the table using HTML_END_TABLE
// 4. Follow the above sequence for each table in the page.
// 5. At the end of the page add the javascript to perform the sort and filter functions using
//      HTML_ADD_SORT_AND_FILTER_TABLE_SCRIPT

#define INTERNAL_HTML_INTERNAL_PRINT_HEADER_FIELDS(i, table_name, field) \
  output << "<th onclick=\"sortTable('" << table_name << "', " << _header_cnt << ")\">" \
         << ::yb::AsString(field) << "</th>"; \
  ++_header_cnt;

#define INTERNAL_HTML_PRINT_TABLE_WITH_HEADER_ROW(table_id, ...) \
  do { \
    const auto table_name = table_id + "_table"; \
    const auto table_filter = table_id + "_filter"; \
    output << GenerateTableFilterBox(table_filter, table_name); \
    uint32 _header_cnt = 0; \
    output << "<table class='table table-striped' id='" << table_name << "'>\n"; \
    output << "<tr>"; \
    BOOST_PP_SEQ_FOR_EACH( \
        INTERNAL_HTML_INTERNAL_PRINT_HEADER_FIELDS, table_name, \
        BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__)) \
    output << "</tr>\n"; \
  } while (0)

#define HTML_PRINT_TABLE_WITH_HEADER_ROW(table_type, ...) \
  HTML_PRINT_TABLE_WITH_HEADER_ROW_WITH_ID(table_type, 0, __VA_ARGS__)

#define HTML_PRINT_TABLE_WITH_HEADER_ROW_WITH_ID(table_type, type_id, ...) \
  INTERNAL_HTML_PRINT_TABLE_WITH_HEADER_ROW( \
      (std::string(BOOST_PP_STRINGIZE(table_type)) + ::yb::AsString(type_id)), __VA_ARGS__)

#define HTML_INTERNAL_PRINT_ROW_FIELDS(i, data, field) "<td>" << ::yb::AsString(field) << "</td>" <<
#define HTML_PRINT_TABLE_ROW(...) \
  output << "<tr>" \
          << BOOST_PP_SEQ_FOR_EACH( \
                 HTML_INTERNAL_PRINT_ROW_FIELDS, ~, \
                 BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__)) "</tr>\n"

#define HTML_END_TABLE output << "</table>\n"

#define HTML_ADD_SORT_AND_FILTER_TABLE_SCRIPT output << GetSortAndFilterTableScript()

namespace yb {

std::string GenerateTableFilterBox(const std::string& input_id, const std::string& table_id);

std::string GetSortAndFilterTableScript();

}  // namespace yb
