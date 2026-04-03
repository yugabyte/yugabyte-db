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

// Helper class to print HTML.
class HtmlPrintHelper {
 public:
  explicit HtmlPrintHelper(std::ostream& output);

  HtmlTablePrintHelper CreateTablePrinter(
      std::string table_name, std::vector<std::string> column_names,
      std::vector<HtmlTableCellAlignment> column_html_arguments = {});
  HtmlTablePrintHelper CreateTablePrinter(
      const std::string& table_name, uint32 table_id, std::vector<std::string> column_names);

  HtmlFieldsetScope CreateFieldset(const std::string& name);

  ~HtmlPrintHelper();

 private:
  friend class HtmlTablePrintHelper;

  std::ostream& output_;
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
  struct TableColumnValue {
    std::string value;
    // Sorting by column will first sort by sort_order, then by value.
    size_t sort_order = 0;
    size_t rowspan = 1;
  };

  class TableRow {
   public:
    template <typename T>
    void AddColumn(T&& cell_value, size_t sort_order = 0, size_t rowspan = 1) {
      column_values_.emplace_back(ProcessValue(std::forward<T>(cell_value)), sort_order, rowspan);
    }

    template<typename... Ts>
    void AddColumns(Ts&&... values) {
      (AddColumn(std::forward<Ts>(values)), ...);
    }

   private:
    std::string ProcessValue(std::string&& value) { return std::move(value); }
    std::string ProcessValue(std::string_view value) { return std::string(value); }
    std::string ProcessValue(const char* value) { return value; }
    std::string ProcessValue(bool value) { return value ? "true" : "false"; }
    template<typename T>
    std::string ProcessValue(T&& value) { return yb::AsString(std::forward<T>(value)); }

    friend class HtmlTablePrintHelper;
    std::vector<TableColumnValue> column_values_;
  };

  struct TableRowSet {
   public:
    TableRow& AddRow();

    template<typename... Ts>
    void AddRow(Ts&&... column_values) {
      AddRow().AddColumns(std::forward<Ts>(column_values)...);
    }

   private:
    friend class HtmlTablePrintHelper;
    std::vector<TableRow> rows_;
  };

  TableRowSet& AddRowSet();

  // AddRow helpers are for the common case with one row per row set.
  TableRow& AddRow() {
    return AddRowSet().AddRow();
  }
  template<typename... Ts>
  void AddRow(Ts&&... column_values) {
    AddRow().AddColumns(std::forward<Ts>(column_values)...);
  }

  void Print();

  ~HtmlTablePrintHelper();

 private:
  friend class HtmlPrintHelper;

  HtmlTablePrintHelper(
      std::ostream& output, std::string table_name, std::vector<std::string> column_names,
      std::vector<HtmlTableCellAlignment> column_alignment);

  size_t ColumnCount() const { return column_names_.size(); }

  std::ostream& output_;
  const std::string table_name_;
  const std::vector<std::string> column_names_;
  const std::vector<HtmlTableCellAlignment> column_alignment_;
  std::vector<TableRowSet> table_row_sets_;
};

// Helper class to print HTML fieldset in the current scope.
// Fieldset is closed when the object goes out of scope.
class HtmlFieldsetScope {
 public:
  ~HtmlFieldsetScope();

 private:
  friend class HtmlPrintHelper;
  explicit HtmlFieldsetScope(std::ostream& output, const std::string& name);

  std::ostream& output_;
};

}  // namespace yb
