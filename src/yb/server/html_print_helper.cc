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

#include "yb/server/html_print_helper.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"

namespace yb {

namespace {

// This script is used to sort and filter tables in the html page.
const char* const kSortAndFilterTableScript = R"(
<script>
function castIfNumber(elem) {
  const byte_units = "BKMGTPE";

  // Put empty strings at end of number column.
  if (!elem.length) {
    return Infinity;
  }

  if (elem.length <= 15) {
    const num = Number(elem);
    if (!isNaN(num)) {
      return num;
    }

    // Sorting for HumanReadableNumBytes::ToString.
    const byte_unit = byte_units.indexOf(elem[elem.length - 1]);
    if (byte_unit !== -1) {
      const byte_num = Number(elem.substring(0, elem.length - 1));
      if (!isNaN(byte_num)) {
        return byte_num * Math.pow(2, 10 * byte_unit);
      }
    }
  }

  return elem;
}

function normalizeString(elem) {
  return elem.toLowerCase();
}

function sortTable(table_id, n) {
  const asc_symb = ' <span style="color: grey">\u25B2</span>';
  const desc_symb = ' <span style="color: grey">\u25BC</span>';
  const table = document.getElementById(table_id);
  if (table.rows.length < 3) {
    return;
  }

  const tbody = table.tBodies[0];
  const rows = new Array(...tbody.children);
  const header_row = rows.shift();

  const asc = !header_row.getElementsByTagName("TH")[n].innerHTML.includes(asc_symb);

  for (let j = 0; j < header_row.children.length; ++j) {
    const header = header_row.children[j];
    let contents = header.innerHTML;
    contents = contents.replace(asc_symb, "").replace(desc_symb, "");
    if (j == n) {
      sort_symb = asc ? asc_symb : desc_symb;
      contents += sort_symb;
    }
    header.innerHTML = contents;
  }

  let all_number = true;
  for (const row of rows) {
    all_number = all_number && typeof castIfNumber(row.children[n].innerText) == 'number';
  }

  if (all_number) {
    rows.sort((x, y) => {
      const cmpX = castIfNumber(x.children[n].innerText);
      const cmpY = castIfNumber(y.children[n].innerText);

      return asc ? cmpX - cmpY : cmpY - cmpX;
    });
  } else {
    rows.sort((x, y) => {
      const cmpX = normalizeString(x.children[n].innerText);
      const cmpY = normalizeString(y.children[n].innerText);

      return (asc ? 1 : -1) * cmpX.localeCompare(cmpY);
    });
  }

  tbody.replaceChildren(header_row, ...rows);
}

function filterTableFunction(input_id, table_id) {
  var filter = document.getElementById(input_id).value.toLowerCase();
  var table = document.getElementById(table_id);
  var tr = table.getElementsByTagName("tr");
  for (var i = 0; i < tr.length; i++) {
    if (tr[i].getElementsByTagName("th").length > 0) {
     // Ignore header rows.
      continue;
    }
    var row = tr[i].getElementsByTagName("td");
    var found = false;
    for (const td of row) {
      if (td) {
        var value = td.textContent || td.innerText;
        if (value.toLowerCase().indexOf(filter) > -1) {
          found = true;
          break;
        }
      }
    }

    if(found) {
      tr[i].style.display = "";
    } else {
      tr[i].style.display = "none";
    }
  }
}
</script>
)";

}  // namespace

// ================================================================================
// HtmlPrintHelper
// ================================================================================

HtmlPrintHelper::HtmlPrintHelper(std::stringstream& output) : output_(output) {}

HtmlPrintHelper::~HtmlPrintHelper() {
  if (has_tables_) {
    // Include the script to sort and filter tables if we have any tables.
    output_ << kSortAndFilterTableScript;
  }
}

HtmlTablePrintHelper HtmlPrintHelper::CreateTablePrinter(
    std::string table_name, std::vector<std::string> column_names) {
  has_tables_ = true;

  return HtmlTablePrintHelper(output_, std::move(table_name), std::move(column_names));
}

HtmlTablePrintHelper HtmlPrintHelper::CreateTablePrinter(
    const std::string& table_name, uint32 table_id, std::vector<std::string> column_names) {
  return CreateTablePrinter(Format("$0_$1", table_name, table_id), std::move(column_names));
}

HtmlFieldsetScope HtmlPrintHelper::CreateFieldset(const std::string& name) {
  return HtmlFieldsetScope(output_, name);
}

// ================================================================================
// HtmlTablePrintHelper
// ================================================================================

HtmlTablePrintHelper::HtmlTablePrintHelper(
    std::stringstream& output, std::string table_name, std::vector<std::string> column_names)
    : output_(output), table_name_(std::move(table_name)), column_names_(std::move(column_names)) {
  DCHECK_GT(column_names_.size(), 0);
}

HtmlTablePrintHelper::~HtmlTablePrintHelper() {}

HtmlTablePrintHelper::TableRow& HtmlTablePrintHelper::AddRow() {
  table_rows_.emplace_back();
  return table_rows_.back();
}

void HtmlTablePrintHelper::TableRow::AddColumn(const char* cell_value) {
  column_values_.emplace_back(cell_value);
}

void HtmlTablePrintHelper::TableRow::AddColumn(const std::string& cell_value) {
  AddColumn(cell_value.c_str());
}
void HtmlTablePrintHelper::TableRow::AddColumn(bool cell_value) {
  AddColumn(cell_value ? "true" : "false");
}

void HtmlTablePrintHelper::Print() {
  // Print the search box.
  const auto table_filter = table_name_ + "_filter";
  output_ << "<input type='text' id='" << table_filter << "' onkeyup='filterTableFunction(\""
          << table_filter << "\", \"" << table_name_
          << "\")' placeholder='Search for ...' title='Type in a text'>\n";

  // Print the table definition.
  output_ << "<table class='table table-striped' id='" << table_name_
          << "' style='border: solid; border-width: thin;padding: 10px 10px;border-color:  "
             "#a8a8a8;'>\n";
  output_ << "<tr>";

  // Print the table header row.
  uint32 _header_cnt = 0;
  for (const auto& column : column_names_) {
    output_ << "<th onclick=\"sortTable('" << table_name_ << "', " << _header_cnt << ")\">"
            << column << "</th>";
    ++_header_cnt;
  }

  output_ << "</tr>\n";

  // Print the table rows.
  for (const auto& row : table_rows_) {
    DCHECK_EQ(row.column_values_.size(), column_names_.size());
    output_ << "<tr>";
    for (const auto& column : row.column_values_) {
      output_ << "<td>" << column << "</td>";
    }
    output_ << "</tr>\n";
  }

  output_ << "</table>\n";
}

// ================================================================================
// HtmlFieldsetScope
// ================================================================================

namespace {
static constexpr auto kFieldsetStart =
    "<br><fieldset style=\"border: solid; border-width: thin;padding: 10px 10px;border-color:  "
    "#a8a8a8;\">\n";
static constexpr auto kFieldsetEnd = "</fieldset>\n";
static constexpr auto kFieldsetLegendStart =
    "<legend visible=\"true\" style=\"width:auto;padding: 0px 10px;\">";
static constexpr auto kFieldsetLegendEnd = "</legend>\n";
}  // namespace

HtmlFieldsetScope::HtmlFieldsetScope(std::stringstream& output, const std::string& name)
    : output_(output) {
  output_ << kFieldsetStart;
  output_ << kFieldsetLegendStart << name << kFieldsetLegendEnd;
}
HtmlFieldsetScope::~HtmlFieldsetScope() { output_ << kFieldsetEnd; }

}  // namespace yb
