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

#include <sstream>

#include "yb/util/enums.h"
#include "yb/util/tostring.h"

namespace yb {

class HtmlTablePrintHelper;
class HtmlFieldsetScope;

YB_DEFINE_ENUM(HtmlTableCellAlignment, (Left)(Right));
YB_DEFINE_ENUM(HtmlTableRowColor, (Default)(Red)(Yellow));

// Helper class to print HTML.
class HtmlPrintHelper {
 public:
  explicit HtmlPrintHelper(std::stringstream& output);

  HtmlTablePrintHelper CreateTablePrinter(
      std::string table_name, std::vector<std::string> column_names,
      std::vector<HtmlTableCellAlignment> column_html_arguments = {});
  HtmlTablePrintHelper CreateTablePrinter(
      const std::string& table_name, uint32 table_id, std::vector<std::string> column_names);

  HtmlFieldsetScope CreateFieldset(const std::string& name);

  ~HtmlPrintHelper();

 private:
  friend class HtmlTablePrintHelper;

  std::stringstream& output_;
  bool has_tables_ = false;
};

// Helper class to print HTML tables.
// Table can be sorted by clicking on the column header.
// Table will have a search box to filter rows.
// Table is closed when the object goes out of scope.
// Usage:
// Create using the HtmlPrintHelper::CreateTablePrinter method.
// Ex:
//  HtmlPrintHelper print_helper(output);
// HtmlTablePrintHelper table_printer = print_helper.CreateTablePrinter("Table name",
//    {"Column1","Column2"});
// table_printer.AddRow("Value1","Value2");
// table_printer.Print();
class HtmlTablePrintHelper {
 public:
  struct TableRow {
    std::vector<std::string> column_values_;
    HtmlTableRowColor color_ = HtmlTableRowColor::Default;

    template <typename T>
    void AddColumn(const T& cell_value) {
      AddColumn(yb::AsString(cell_value));
    }

    void AddColumn(const char* cell_value);
    void AddColumn(const std::string& cell_value);
    void AddColumn(bool cell_value);

    template <typename... Ts>
    void AddColumns(const Ts&... values) {
      (AddColumn(values), ...);
    }

    void SetColor(HtmlTableRowColor color) { color_ = color; }
    void SetColor(const Status& status) {
      color_ = status.ok() ? HtmlTableRowColor::Default : HtmlTableRowColor::Red;
    }
  };

  template <typename... Ts>
  TableRow& AddRow(Ts&&... column_values) {
    auto& row = AddRow();
    row.AddColumns(std::forward<Ts>(column_values)...);
    return row;
  }

  HtmlTablePrintHelper::TableRow& AddRow();

  void Print();

  ~HtmlTablePrintHelper();

 private:
  friend class HtmlPrintHelper;

  HtmlTablePrintHelper(
      std::stringstream& output, std::string table_name, std::vector<std::string> column_names,
      std::vector<HtmlTableCellAlignment> column_alignment);

  size_t ColumnCount() const { return column_names_.size(); }

  std::stringstream& output_;
  const std::string table_name_;
  const std::vector<std::string> column_names_;
  const std::vector<HtmlTableCellAlignment> column_alignment_;
  std::vector<TableRow> table_rows_;
};

// Helper class to print HTML fieldset in the current scope.
// Fieldset is closed when the object goes out of scope.
class HtmlFieldsetScope {
 public:
  ~HtmlFieldsetScope();

 private:
  friend class HtmlPrintHelper;
  explicit HtmlFieldsetScope(std::stringstream& output, const std::string& name);

  std::stringstream& output_;
};

}  // namespace yb
