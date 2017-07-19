//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// This module defines the ResultSet that QL database returns to a query request.
// ResultSet is a set of rows of data that is returned by a query request, and each of selected row
// is name "rsrow" in our code. We don't call "rsrow" tuple to avoid conflict with TUPLE datatype
// in Apache Cassandra.
// - Within our code, we call it "rsrow" instead of row to distinguish between selected-rows and
//   rows of a table in the database.
// - Similarly, we use "rscol" in place of "column".
// - To end users, at a high level interface, we would call them all as rows & columns.
//
// NOTE:
// - This should be merged or shared a super class with ql_rowblock.cc.
// - This will be done in the next diff. We don't do this now to avoid large code modifications.
//--------------------------------------------------------------------------------------------------

#ifndef YB_COMMON_QL_RESULTSET_H_
#define YB_COMMON_QL_RESULTSET_H_

#include "yb/common/ql_value.h"

namespace yb {

//--------------------------------------------------------------------------------------------------
// A rsrow descriptor represents the metadata of a row in the resultset.
class QLRSRowDesc {
 public:
  class RSColDesc {
   public:
    RSColDesc() {
    }
    RSColDesc(const string& name, const QLType::SharedPtr& ql_type)
        : name_(name), ql_type_(ql_type) {
    }
    const string& name() {
      return name_;
    }
    const QLType::SharedPtr& ql_type() {
      return ql_type_;
    }
   private:
    string name_;
    QLType::SharedPtr ql_type_;
  };

  explicit QLRSRowDesc(const QLRSRowDescPB& desc_pb);
  virtual ~QLRSRowDesc();

  int32_t rscol_count() const {
    return rscol_descs_.size();
  }

  const vector<RSColDesc>& rscol_descs() const {
    return rscol_descs_;
  }

 private:
  vector<RSColDesc> rscol_descs_;
};

//--------------------------------------------------------------------------------------------------
// A rsrow represents the values of a row in the resultset.
class QLRSRow {
 public:
  explicit QLRSRow(int32_t rscol_count);
  virtual ~QLRSRow();

  const std::vector<QLValueWithPB>& rscols() const {
    return rscols_;
  }

  size_t rscol_count() const { return rscols_.size(); }

  QLValueWithPB *rscol(int32_t index) {
    return &rscols_[index];
  }

  CHECKED_STATUS CQLSerialize(const QLClient& client,
                              const QLRSRowDesc& rsrow_desc,
                              faststring* buffer) const;

 private:
  std::vector<QLValueWithPB> rscols_;
};

// A set of rsrows.
class QLResultSet {
 public:
  typedef std::shared_ptr<QLResultSet> SharedPtr;

  // Constructor and destructor.
  QLResultSet();
  virtual ~QLResultSet();

  const std::vector<QLRSRow>& rsrows() const {
    return rsrows_;
  }

  // Allocate a new rsrow and append it to the end of result set.
  QLRSRow *AllocateRSRow(int32_t rscol_count);

  // Row count
  size_t rsrow_count() const { return rsrows_.size(); }

  // Serialization routines with CQL encoding format.
  CHECKED_STATUS CQLSerialize(const QLClient& client,
                              const QLRSRowDesc& rsrow_desc,
                              faststring* buffer) const;

 private:
  std::vector<QLRSRow> rsrows_;
};

} // namespace yb

#endif // YB_COMMON_QL_RESULTSET_H_
