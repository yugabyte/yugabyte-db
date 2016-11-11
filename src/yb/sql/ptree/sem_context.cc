//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/session_context.h"
#include "yb/sql/ptree/sem_context.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------

SemContext::SemContext(const char *sql_stmt,
                       size_t stmt_len,
                       ParseTree::UniPtr parse_tree,
                       SessionContext *session_context,
                       int retry_count)
    : ProcessContext(sql_stmt, stmt_len, move(parse_tree)),
      symtab_(ptemp_mem_.get()),
      session_context_(session_context),
      retry_count_(retry_count) {
}

SemContext::~SemContext() {
}

//--------------------------------------------------------------------------------------------------

void SemContext::MapSymbol(const MCString& name, PTColumnDefinition *entry) {
  if (symtab_[name].column_ != nullptr) {
    Error(entry->loc(), ErrorCode::DUPLICATE_COLUMN);
  }
  symtab_[name].column_ = entry;
}

void SemContext::MapSymbol(const MCString& name, PTCreateTable *entry) {
  if (symtab_[name].table_ != nullptr) {
    Error(entry->loc(), ErrorCode::DUPLICATE_TABLE);
  }
  symtab_[name].table_ = entry;
}

const SymbolEntry *SemContext::SeekSymbol(const MCString& name) {
  auto iter = symtab_.find(name);
  if (iter != symtab_.end()) {
    return &iter->second;
  }
  return nullptr;
}

//--------------------------------------------------------------------------------------------------

ConversionMode SemContext::GetConversionMode(client::YBColumnSchema::DataType lhs_type,
                                             client::YBColumnSchema::DataType rhs_type) {
  static const ConversionMode kIM = ConversionMode::kImplicit;
  static const ConversionMode kEX = ConversionMode::kExplicit;
  static const ConversionMode kNA = ConversionMode::kNotAllowed;
  static const int max_index = client::YBColumnSchema::MAX_TYPE_INDEX;
  static const ConversionMode conversion_mode[max_index][max_index] = {
    // RHS (source)
    // i8  | i16 | i32 | i64 | str | bool | flt | dbl | bin | tst                    // LHS (dest)
    { kIM,  kIM,  kIM,  kIM,  kIM,  kNA,   kIM,  kIM,  kEX,  kNA  },                 // int8
    { kIM,  kIM,  kIM,  kIM,  kIM,  kNA,   kIM,  kIM,  kEX,  kNA  },                 // int16
    { kIM,  kIM,  kIM,  kIM,  kIM,  kNA,   kIM,  kIM,  kEX,  kNA  },                 // int32
    { kIM,  kIM,  kIM,  kIM,  kIM,  kNA,   kIM,  kIM,  kEX,  kNA  },                 // int64
    { kIM,  kIM,  kIM,  kIM,  kIM,  kNA,   kIM,  kIM,  kEX,  kNA  },                 // string
    { kNA,  kNA,  kNA,  kNA,  kNA,  kIM,   kNA,  kNA,  kNA,  kNA  },                 // bool
    { kIM,  kIM,  kIM,  kIM,  kIM,  kNA,   kIM,  kIM,  kEX,  kNA  },                 // float
    { kIM,  kIM,  kIM,  kIM,  kIM,  kNA,   kIM,  kIM,  kEX,  kNA  },                 // double
    { kEX,  kEX,  kEX,  kEX,  kEX,  kNA,   kEX,  kEX,  kIM,  kNA  },                 // bin
    { kNA,  kNA,  kNA,  kNA,  kNA,  kNA,   kNA,  kNA,  kNA,  kIM  },                 // timestamp
  };

  return conversion_mode[rhs_type][lhs_type];
}

}  // namespace sql
}  // namespace yb
