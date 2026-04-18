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

#include "yb/util/html_print_helper.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"

namespace yb {

namespace {

std::string ColumnAlignmentToHtmlAttr(HtmlTableCellAlignment alignment) {
  switch (alignment) {
    case HtmlTableCellAlignment::Left:
      return "align=\"left\"";
    case HtmlTableCellAlignment::Right:
      return "align=\"right\"";
  }
  FATAL_INVALID_ENUM_VALUE(HtmlTableCellAlignment, alignment);
}

const char* RowColorToStyle(HtmlTableRowColor color) {
  switch (color) {
    case HtmlTableRowColor::Red:
      return " style=\"background-color: #f8d7da\"";
    case HtmlTableRowColor::Yellow:
      return " style=\"background-color: #fff3cd\"";
    case HtmlTableRowColor::Default:
      return "";
  }
  FATAL_INVALID_ENUM_VALUE(HtmlTableRowColor, color);
}

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

  const tbodies = new Array(...table.children);
  const thead = tbodies.shift();
  const header_row = thead.children[0];

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
  for (const tbody of tbodies) {
    const sort_row = tbody.children[0];
    all_number = all_number && typeof castIfNumber(sort_row.children[n].innerText) == 'number';
  }

  let compare_text;
  if (all_number) {
    compare_text = (x, y) => {
      const cmpX = castIfNumber(x);
      const cmpY = castIfNumber(y);

      return asc ? cmpX - cmpY : cmpY - cmpX;
    };
  } else {
    compare_text = (x, y) => {
      const cmpX = normalizeString(x);
      const cmpY = normalizeString(y);

      return (asc ? 1 : -1) * cmpX.localeCompare(cmpY);
    };
  }
  tbodies.sort((x, y) => {
    const tdX = x.children[0].children[n];
    const tdY = y.children[0].children[n];
    const sortX = +tdX.getAttribute('data-sort');
    const sortY = +tdY.getAttribute('data-sort');
    if (sortX != sortY) {
      return asc ? sortX - sortY : sortY - sortX;
    }
    return compare_text(tdX.innerText, tdY.innerText);
  });

  table.replaceChildren(thead, ...tbodies);
}

function filterTableFunction(input_id, table_id) {
  const filter = document.getElementById(input_id).value.toLowerCase();
  const table = document.getElementById(table_id);
  const tbodies = table.querySelectorAll("tbody");
  for (const tbody of tbodies) {
    const tds = tbody.querySelectorAll("td");
    let found = false;
    for (const td of tds) {
      const value = td.textContent || td.innerText;
      if (value.toLowerCase().indexOf(filter) > -1) {
        found = true;
        break;
      }
    }

    if(found) {
      tbody.style.display = "";
    } else {
      tbody.style.display = "none";
    }
  }
}
</script>
)";

}  // namespace

// ================================================================================
// HtmlPrintHelper
// ================================================================================

HtmlPrintHelper::HtmlPrintHelper(std::ostream& output) : output_(output) {}

HtmlPrintHelper::~HtmlPrintHelper() {
  if (has_tables_) {
    // Include the script to sort and filter tables if we have any tables.
    output_ << kSortAndFilterTableScript;
  }
}

HtmlTablePrintHelper HtmlPrintHelper::CreateTablePrinter(
    std::string table_name, std::vector<std::string> column_names,
    std::vector<HtmlTableCellAlignment> column_alignment) {
  has_tables_ = true;

  return HtmlTablePrintHelper(
      output_, std::move(table_name), std::move(column_names), std::move(column_alignment));
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
    std::ostream& output, std::string table_name, std::vector<std::string> column_names,
    std::vector<HtmlTableCellAlignment> column_alignment)
    : output_(output), table_name_(std::move(table_name)), column_names_(std::move(column_names)),
      column_alignment_(std::move(column_alignment)) {
  DCHECK_GT(column_names_.size(), 0);
}

HtmlTablePrintHelper::~HtmlTablePrintHelper() {}

HtmlTablePrintHelper::TableRowSet& HtmlTablePrintHelper::AddRowSet() {
  table_row_sets_.emplace_back();
  return table_row_sets_.back();
}

HtmlTablePrintHelper::TableRow& HtmlTablePrintHelper::TableRowSet::AddRow() {
  rows_.emplace_back();
  return rows_.back();
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
  output_ << "<thead><tr>";

  // Print the table header row.
  uint32 _header_cnt = 0;
  for (const auto& column : column_names_) {
    output_ << "<th onclick=\"sortTable('" << table_name_ << "', " << _header_cnt << ")\">"
            << column << "</th>";
    ++_header_cnt;
  }

  output_ << "</tr></thead>\n";

  for (const auto& row_set : table_row_sets_) {
    output_ << "<tbody>";
    for (const auto& row : row_set.rows_) {
      output_ << "<tr" << RowColorToStyle(row.color_) << ">";
      for (size_t i = 0; i < row.column_values_.size(); ++i) {
        const auto& column_value = row.column_values_[i];
        output_ << "<td";
        if (i < column_alignment_.size()) {
          output_ << " " << ColumnAlignmentToHtmlAttr(column_alignment_[i]);
        }
        if (column_value.rowspan > 1) {
          output_ << " rowspan=" << column_value.rowspan;
        }
        output_ << " data-sort=" << column_value.sort_order;
        output_ << ">" << column_value.value << "</td>";
      }
      output_ << "</tr>";
    }
    output_ << "</tbody>\n";
  }

  output_ << "</table>\n";
}

// ================================================================================
// HtmlFieldsetScope
// ================================================================================

namespace {
static constexpr auto kFieldsetStart =
    "<br><fieldset style=\"border: solid; border-width: thin;padding: 10px 10px;border-color:  "
    "#a8a8a8;overflow-x: auto;\">\n";
static constexpr auto kFieldsetEnd = "</fieldset>\n";
static constexpr auto kFieldsetLegendStart =
    "<legend visible=\"true\" style=\"width:auto;padding: 0px 10px;\">";
static constexpr auto kFieldsetLegendEnd = "</legend>\n";
}  // namespace

HtmlFieldsetScope::HtmlFieldsetScope(std::ostream& output, const std::string& name)
    : output_(output) {
  output_ << kFieldsetStart;
  output_ << kFieldsetLegendStart << name << kFieldsetLegendEnd;
}
HtmlFieldsetScope::~HtmlFieldsetScope() { output_ << kFieldsetEnd; }

}  // namespace yb
