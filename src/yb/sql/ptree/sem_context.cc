//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/util/sql_env.h"
#include "yb/sql/ptree/sem_context.h"

namespace yb {
namespace sql {

using std::shared_ptr;
using client::YBTable;

//--------------------------------------------------------------------------------------------------

SemContext::SemContext(const char *sql_stmt,
                       size_t stmt_len,
                       ParseTree::UniPtr parse_tree,
                       SqlEnv *sql_env,
                       bool refresh_cache)
    : ProcessContext(sql_stmt, stmt_len, move(parse_tree)),
      symtab_(ptemp_mem_.get()),
      sql_env_(sql_env),
      refresh_cache_(refresh_cache),
      cache_used_(false) {
}

SemContext::~SemContext() {
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS SemContext::MapSymbol(const MCString& name, PTColumnDefinition *entry) {
  if (symtab_[name].column_ != nullptr) {
    RETURN_NOT_OK(Error(entry->loc(), ErrorCode::DUPLICATE_COLUMN));
  }
  symtab_[name].column_ = entry;
  return Status::OK();
}

CHECKED_STATUS SemContext::MapSymbol(const MCString& name, PTCreateTable *entry) {
  if (symtab_[name].table_ != nullptr) {
    RETURN_NOT_OK(Error(entry->loc(), ErrorCode::DUPLICATE_TABLE));
  }
  symtab_[name].table_ = entry;
  return Status::OK();
}

CHECKED_STATUS SemContext::MapSymbol(const MCString& name, ColumnDesc *entry) {
  if (symtab_[name].column_desc_ != nullptr) {
    LOG(FATAL) << "Entries of the same symbol are inserted"
               << ", Existing entry = " << symtab_[name].column_desc_
               << ", New entry = " << entry;
  }
  symtab_[name].column_desc_ = entry;
  return Status::OK();
}

shared_ptr<YBTable> SemContext::GetTableDesc(const client::YBTableName& table_name) {
  bool cache_used = false;
  shared_ptr<YBTable> table = sql_env_->GetTableDesc(table_name, refresh_cache_, &cache_used);
  if (cache_used) {
    // Remember cache was used.
    cache_used_ = true;
  }
  return table;
}

const SymbolEntry *SemContext::SeekSymbol(const MCString& name) const {
  auto iter = symtab_.find(name);
  if (iter != symtab_.end()) {
    return &iter->second;
  }
  return nullptr;
}

PTColumnDefinition *SemContext::GetColumnDefinition(const MCString& col_name) const {
  const SymbolEntry * entry = SeekSymbol(col_name);
  if (entry == nullptr) {
    return nullptr;
  }
  return entry->column_;
}

const ColumnDesc *SemContext::GetColumnDesc(const MCString& col_name) const {
  const SymbolEntry * entry = SeekSymbol(col_name);
  if (entry == nullptr) {
    return nullptr;
  }
  return entry->column_desc_;
}

//--------------------------------------------------------------------------------------------------
// Utilities for Convertible and Comparable checks below
// We map supported types to consecutive indexes so we can write down convertible/comparable
// functions concisely using a matrix for each.
// TODO (mihnea) this code is temporary: should be removed/simplified as part of cleaning up the
// DataType enum to separate our CQL/Client types from the Kudu-inherited internal types

const DataType kSupportedTypes[] =
    {INT8, INT16, INT32, INT64, STRING, BOOL, FLOAT, DOUBLE, BINARY, TIMESTAMP, NULL_VALUE_TYPE};

const std::map<DataType, int> MakeTypesIndex() {
  std::map<DataType, int> type_indexes;
  int size = sizeof(kSupportedTypes)/sizeof(kSupportedTypes[0]);
  for (int i = 0; i < size; ++i) {
    type_indexes.insert({kSupportedTypes[i], i});
  }
  return type_indexes;
}

const std::map<DataType, int> kTypesIndex = MakeTypesIndex();

//--------------------------------------------------------------------------------------------------

ConversionMode SemContext::GetConversionMode(DataType lhs_type,
                                             DataType rhs_type) const {
  static const ConversionMode kIM = ConversionMode::kImplicit;
  static const ConversionMode kEX = ConversionMode::kExplicit;
  static const ConversionMode kNA = ConversionMode::kNotAllowed;
  static const int max_index = sizeof(kSupportedTypes)/sizeof(kSupportedTypes[0]); // TODO fix this
  static const ConversionMode conversion_mode[max_index][max_index] = {
    // RHS (source)
    // i8  | i16 | i32 | i64 | str | bool | flt | dbl | bin | tst | null             // LHS (dest)
    { kIM,  kIM,  kIM,  kIM,  kNA,  kNA,   kEX,  kEX,  kNA,  kNA,  kIM },            // int8
    { kIM,  kIM,  kIM,  kIM,  kNA,  kNA,   kEX,  kEX,  kNA,  kNA,  kIM },            // int16
    { kIM,  kIM,  kIM,  kIM,  kNA,  kNA,   kEX,  kEX,  kNA,  kNA,  kIM },            // int32
    { kIM,  kIM,  kIM,  kIM,  kNA,  kNA,   kEX,  kEX,  kNA,  kEX,  kIM },            // int64
    { kNA,  kNA,  kNA,  kNA,  kIM,  kNA,   kNA,  kNA,  kNA,  kEX,  kIM },            // string
    { kNA,  kNA,  kNA,  kNA,  kNA,  kIM,   kNA,  kNA,  kNA,  kNA,  kIM },            // bool
    { kIM,  kIM,  kIM,  kIM,  kNA,  kNA,   kIM,  kNA,  kNA,  kNA,  kIM },            // float
    { kIM,  kIM,  kIM,  kIM,  kNA,  kNA,   kNA,  kIM,  kNA,  kNA,  kIM },            // double
    { kNA,  kNA,  kNA,  kNA,  kNA,  kNA,   kNA,  kNA,  kIM,  kNA,  kIM },            // bin
    { kNA,  kNA,  kNA,  kIM,  kIM,  kNA,   kNA,  kNA,  kNA,  kIM,  kIM },            // timestamp
    { kNA,  kNA,  kNA,  kNA,  kNA,  kNA,   kNA,  kNA,  kNA,  kNA,  kNA },            // null
  };

  return conversion_mode[kTypesIndex.at(lhs_type)][kTypesIndex.at(rhs_type)];
}

bool SemContext::IsComparable(DataType lhs_type,
                              DataType rhs_type) const {
  static const bool kYS = true;
  static const bool kNO = false;
  static const int max_index = sizeof(kSupportedTypes)/sizeof(kSupportedTypes[0]); // TODO fix this
  static const bool compare_mode[max_index][max_index] = {
    // RHS (source)
    // i8  | i16 | i32 | i64 | str | bool | flt | dbl | bin | tst | null             // LHS (dest)
    { kYS,  kYS,  kYS,  kYS,  kNO,  kNO,   kNO,  kNO,  kNO,  kNO,  kNO },            // int8
    { kYS,  kYS,  kYS,  kYS,  kNO,  kNO,   kNO,  kNO,  kNO,  kNO,  kNO },            // int16
    { kYS,  kYS,  kYS,  kYS,  kNO,  kNO,   kNO,  kNO,  kNO,  kNO,  kNO },            // int32
    { kYS,  kYS,  kYS,  kYS,  kNO,  kNO,   kNO,  kNO,  kNO,  kNO,  kNO },            // int64
    { kNO,  kNO,  kNO,  kNO,  kYS,  kNO,   kNO,  kNO,  kNO,  kNO,  kNO },            // string
    { kNO,  kNO,  kNO,  kNO,  kNO,  kYS,   kNO,  kNO,  kNO,  kNO,  kNO },            // bool
    { kYS,  kYS,  kYS,  kYS,  kNO,  kNO,   kYS,  kNO,  kNO,  kNO,  kNO },            // float
    { kYS,  kYS,  kYS,  kYS,  kNO,  kNO,   kNO,  kYS,  kNO,  kNO,  kNO },            // double
    { kNO,  kNO,  kNO,  kNO,  kNO,  kNO,   kNO,  kNO,  kYS,  kNO,  kNO },            // bin
    { kNO,  kNO,  kNO,  kYS,  kYS,  kNO,   kNO,  kNO,  kNO,  kYS,  kNO },            // timestamp
    { kNO,  kNO,  kNO,  kNO,  kNO,  kNO,   kNO,  kNO,  kNO,  kNO,  kNO },            // null
  };

  return compare_mode[kTypesIndex.at(lhs_type)][kTypesIndex.at(rhs_type)];
}

}  // namespace sql
}  // namespace yb
