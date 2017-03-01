//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Structure definitions for column descriptor of a table.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PTREE_COLUMN_DESC_H_
#define YB_SQL_PTREE_COLUMN_DESC_H_

#include "yb/client/client.h"
#include "yb/common/types.h"
#include "yb/sql/util/base_types.h"
#include "yb/sql/ptree/pt_type.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------

// This class can be used to describe any reference of a column.
class ColumnDesc {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::shared_ptr<ColumnDesc> SharedPtr;
  typedef std::shared_ptr<const ColumnDesc> SharedPtrConst;

  ColumnDesc()
      : index_(-1),
        id_(-1),
        is_hash_(false),
        is_primary_(false),
        sql_type_(DataType::UNKNOWN_DATA),
        type_id_(InternalType::VALUE_NOT_SET) {
  }

  void Init(int index,
            int id,
            bool is_hash,
            bool is_primary,
            DataType sql_type,
            InternalType type_id) {
    index_ = index,
    id_ = id;
    is_hash_ = is_hash;
    is_primary_ = is_primary;
    sql_type_ = sql_type;
    type_id_ = type_id;
  }

  bool IsInitialized() const {
    return (index_ >= 0);
  }

  int index() const {
    return index_;
  }

  int id() const {
    return id_;
  }

  bool is_hash() const {
    return is_hash_;
  }

  bool is_primary() const {
    return is_primary_;
  }

  DataType sql_type() const {
    return sql_type_;
  }

  InternalType type_id() const {
    return type_id_;
  }

 private:
  int index_;
  int id_;
  bool is_hash_;
  bool is_primary_;
  DataType sql_type_;
  InternalType type_id_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_COLUMN_DESC_H_
