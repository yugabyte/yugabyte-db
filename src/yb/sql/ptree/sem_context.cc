//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/util/sql_env.h"
#include "yb/sql/ptree/sem_context.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------

SemContext::SemContext(const char *sql_stmt,
                       size_t stmt_len,
                       ParseTree::UniPtr parse_tree,
                       SqlEnv *sql_env,
                       int retry_count)
    : ProcessContext(sql_stmt, stmt_len, move(parse_tree)),
      symtab_(ptemp_mem_.get()),
      sql_env_(sql_env),
      retry_count_(retry_count) {
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

ConversionMode SemContext::GetConversionMode(client::YBColumnSchema::DataType lhs_type,
                                             client::YBColumnSchema::DataType rhs_type) const {
  static const ConversionMode kIM = ConversionMode::kImplicit;
  static const ConversionMode kEX = ConversionMode::kExplicit;
  static const ConversionMode kNA = ConversionMode::kNotAllowed;
  static const int max_index = client::YBColumnSchema::MAX_TYPE_INDEX + 1;
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

  return conversion_mode[lhs_type][rhs_type];
}

bool SemContext::IsComparable(client::YBColumnSchema::DataType lhs_type,
                              client::YBColumnSchema::DataType rhs_type) const {
  static const bool kYS = true;
  static const bool kNO = false;
  static const int max_index = client::YBColumnSchema::MAX_TYPE_INDEX + 1;
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

  return compare_mode[lhs_type][rhs_type];
}

}  // namespace sql
}  // namespace yb
