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
  shared_ptr<YBTable> table = sql_env_->GetTableDesc(table_name,
                                                     refresh_cache_,
                                                     &cache_used);
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

bool SemContext::IsConvertible(PTExpr::SharedPtr expr, YQLType type) const {
  switch (type.main()) {
    // Collection types : we only use conversion table for their elements
    case MAP: {
      // the empty set "{}" is a valid map expression
      if (expr->yql_type_id() == SET) {
        PTSetExpr *set_expr = static_cast<PTSetExpr *>(expr.get());
        return set_expr->elems().empty();
      }

      if (expr->yql_type_id() != MAP) {
        return expr->yql_type_id() == NULL_VALUE_TYPE;
      }
      YQLType keys_type = type.params()->at(0);
      YQLType values_type = type.params()->at(1);
      PTMapExpr *map_expr = static_cast<PTMapExpr *>(expr.get());
      for (auto &key : map_expr->keys()) {
        if (!IsConvertible(key, keys_type)) {
          return false;
        }
      }
      for (auto &value : map_expr->values()) {
        if (!IsConvertible(value, values_type)) {
          return false;
        }
      }

      return true;
    }

    case SET: {
      if (expr->yql_type_id() != SET) {
        return expr->yql_type_id() == NULL_VALUE_TYPE;
      }
      YQLType elem_type = type.params()->at(0);
      PTSetExpr *set_expr = static_cast<PTSetExpr*>(expr.get());
      for (auto &elem : set_expr->elems()) {
        if (!IsConvertible(elem, elem_type)) {
          return false;
        }
      }
      return true;
    }

    case LIST: {
      if (expr->yql_type_id() != LIST) {
        return expr->yql_type_id() == NULL_VALUE_TYPE;
      }
      YQLType elem_type = type.params()->at(0);
      PTListExpr *list_expr = static_cast<PTListExpr*>(expr.get());
      for (auto &elem : list_expr->elems()) {
        if (!IsConvertible(elem, elem_type)) {
          return false;
        }
      }
      return true;
    }

    case TUPLE:
      LOG(FATAL) << "Tuple type not support yet";
      return false;

    // Elementary types : we directly check conversion table
    default:
      return YQLType::IsImplicitlyConvertible(type.main(), expr->yql_type_id());
  }
}

bool SemContext::IsComparable(DataType lhs_type, DataType rhs_type) const {
  return YQLType::IsComparable(lhs_type, rhs_type);
}

}  // namespace sql
}  // namespace yb
