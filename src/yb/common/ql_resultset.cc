//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------
#include "yb/common/ql_resultset.h"

#include "yb/common/ql_protocol.pb.h"
#include "yb/common/ql_protocol_util.h"
#include "yb/common/ql_value.h"

namespace yb {
// TODO(neil) All QL classes in "yb/common" needs to be group under a namespace. Doing that would
// result in a number of code changes, so I'll leave this till next diff.
// namespace yqlapi

//--------------------------------------------------------------------------------------------------

QLRSRowDesc::RSColDesc::RSColDesc(const QLRSColDescPB& desc_pb)
  : name_(desc_pb.name()), ql_type_(QLType::FromQLTypePB(desc_pb.ql_type())) {}

QLRSRowDesc::QLRSRowDesc(const QLRSRowDescPB& desc_pb) {
  rscol_descs_.reserve(desc_pb.rscol_descs().size());
  for (const auto& rscol_desc_pb : desc_pb.rscol_descs()) {
    rscol_descs_.emplace_back(rscol_desc_pb);
  }
}

QLRSRowDesc::~QLRSRowDesc() {
}

//--------------------------------------------------------------------------------------------------

QLResultSet::QLResultSet(const QLRSRowDesc* rsrow_desc, faststring* rows_data)
    : rsrow_desc_(rsrow_desc), rows_data_(rows_data) {
  CQLEncodeLength(0, rows_data_);
}

QLResultSet::~QLResultSet() {
}

void QLResultSet::AllocateRow() {
  CQLEncodeLength(CQLDecodeLength(rows_data_->data()) + 1, rows_data_->data());
}

void QLResultSet::AppendColumn(const size_t index, const QLValue& value) {
  value.Serialize(rsrow_desc_->rscol_descs()[index].ql_type(), YQL_CLIENT_CQL, rows_data_);
}

void QLResultSet::AppendColumn(const size_t index, const QLValuePB& value) {
  QLValue::Serialize(
      rsrow_desc_->rscol_descs()[index].ql_type(), YQL_CLIENT_CQL, value, rows_data_);
}

size_t QLResultSet::rsrow_count() const {
  return CQLDecodeLength(rows_data_->data());
}

} // namespace yb
