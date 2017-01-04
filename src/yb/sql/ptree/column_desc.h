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
        sql_type_(client::YBColumnSchema::DataType::MAX_TYPE_INDEX),
        type_id_(yb::DataType::UNKNOWN_DATA) {
  }

  void Init(int index,
            int id,
            bool is_hash,
            bool is_primary,
            client::YBColumnSchema::DataType sql_type,
            yb::DataType type_id) {
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

  client::YBColumnSchema::DataType sql_type() const {
    return sql_type_;
  }

  yb::DataType type_id() const {
    return type_id_;
  }

 private:
  int index_;
  int id_;
  bool is_hash_;
  bool is_primary_;
  client::YBColumnSchema::DataType sql_type_;
  yb::DataType type_id_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_COLUMN_DESC_H_
