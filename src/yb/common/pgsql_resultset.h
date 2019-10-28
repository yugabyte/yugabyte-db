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

#ifndef YB_COMMON_PGSQL_RESULTSET_H_
#define YB_COMMON_PGSQL_RESULTSET_H_

#include "yb/common/common_fwd.h"
#include "yb/common/ql_type.h"

namespace yb {

//--------------------------------------------------------------------------------------------------
// A rsrow descriptor represents the metadata of a row in the resultset.
class PgsqlRSRowDesc {
 public:
  class RSColDesc {
   public:
    RSColDesc() {
    }
    RSColDesc(const std::string& name, const QLType::SharedPtr& ql_type)
        : name_(name), ql_type_(ql_type) {
    }
    const std::string& name() const {
      return name_;
    }
    const QLType::SharedPtr& ql_type() const {
      return ql_type_;
    }
    DataType yql_typeid() const {
      return ql_type_->main();
    }
   private:
    const string name_;
    QLType::SharedPtr ql_type_;
  };

  explicit PgsqlRSRowDesc(const PgsqlRSRowDescPB& desc_pb);
  virtual ~PgsqlRSRowDesc();

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
class PgsqlRSRow {
 public:
  explicit PgsqlRSRow(int32_t rscol_count);
  virtual ~PgsqlRSRow();

  const std::vector<QLValue>& rscols() const {
    return rscols_;
  }

  size_t rscol_count() const;

  const QLValue& rscol_value(int32_t index) const;

  QLValue* rscol(int32_t index);

 private:
  std::vector<QLValue> rscols_;
};

// A set of rsrows.
class PgsqlResultSet {
 public:
  typedef std::shared_ptr<PgsqlResultSet> SharedPtr;

  // Constructor and destructor.
  PgsqlResultSet();
  virtual ~PgsqlResultSet();

  const std::vector<PgsqlRSRow>& rsrows() const {
    return rsrows_;
  }

  // Allocate a new rsrow and append it to the end of result set.
  PgsqlRSRow *AllocateRSRow(int32_t rscol_count);

  // Row count
  size_t rsrow_count() const { return rsrows_.size(); }

 private:
  std::vector<PgsqlRSRow> rsrows_;
};

} // namespace yb

#endif // YB_COMMON_PGSQL_RESULTSET_H_
