//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// This module defines the ResultSet that QL database returns to a query request.
// QLResultSet is a set of rows of data that is returned by a query request.
// - Within our code, we call it "rsrow" instead of row to distinguish between selected-rows and
//   rows of a table in the database.
// - Similarly, we use "rscol" in place of "column".
// - To end users, at a high level interface, we would call them all as rows & columns.
//
// NOTE:
// - This should be merged or shared a super class with ql_rowblock.cc.
// - This will be done in the next diff. We don't do this now to avoid large code modifications.
// - For optimization, columns and rows are serialized (in CQL wire format) directly for return to
//   call. If there is a need to manipulate the rows before return, QLResultSet should be changed to
//   an interface with multiple implementations for different use-cases.
//--------------------------------------------------------------------------------------------------

#pragma once

#include "yb/common/common_fwd.h"
#include "yb/common/ql_type.h"

#include "yb/util/write_buffer.h"

namespace yb::qlexpr {

//--------------------------------------------------------------------------------------------------
// A rsrow descriptor represents the metadata of a row in the resultset.
class QLRSRowDesc {
 public:
  class RSColDesc {
   public:
    explicit RSColDesc(const QLRSColDescPB& desc_pb);

    const std::string& name() const {
      return name_;
    }
    const QLType::SharedPtr& ql_type() const {
      return ql_type_;
    }
   private:
    std::string name_;
    QLType::SharedPtr ql_type_;
  };

  explicit QLRSRowDesc(const QLRSRowDescPB& desc_pb);
  virtual ~QLRSRowDesc();

  size_t rscol_count() const {
    return rscol_descs_.size();
  }

  const std::vector<RSColDesc>& rscol_descs() const {
    return rscol_descs_;
  }

 private:
  std::vector<RSColDesc> rscol_descs_;
};

//--------------------------------------------------------------------------------------------------
// A set of rows.
class QLResultSet {
 public:
  typedef std::shared_ptr<QLResultSet> SharedPtr;

  // Constructor and destructor.
  QLResultSet(const QLRSRowDesc* rsrow_desc, WriteBuffer* rows_data);
  virtual ~QLResultSet();

  // Allocate a new row at the end of result set.
  void AllocateRow();

  // Append a column to the last row in the result set.
  void AppendColumn(size_t index, const QLValue& value);
  void AppendColumn(size_t index, const QLValuePB& value);

  // Row count
  size_t rsrow_count() const {
    return rsrow_count_;
  }

  void Complete();

 private:
  const QLRSRowDesc* rsrow_desc_ = nullptr;
  WriteBuffer* rows_data_ = nullptr;
  WriteBufferPos count_position_;
  size_t rsrow_count_ = 0;
};

}  // namespace yb::qlexpr
