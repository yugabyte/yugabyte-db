//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ptree/sem_context.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------

SemContext::SemContext(const char *sql_stmt, size_t stmt_len, ParseTree::UniPtr parse_tree)
    : ProcessContext(sql_stmt, stmt_len, move(parse_tree)),
      symtab_(ptemp_mem_.get()) {
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

}  // namespace sql
}  // namespace yb
