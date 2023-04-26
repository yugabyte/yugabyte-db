//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------
#include "yb/qlexpr/ql_resultset.h"

#include "yb/common/ql_protocol.pb.h"
#include "yb/common/ql_protocol_util.h"
#include "yb/common/ql_value.h"

#include "yb/gutil/casts.h"

#include "yb/qlexpr/ql_serialization.h"

#include "yb/util/write_buffer.h"

namespace yb::qlexpr {
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

QLResultSet::QLResultSet(const QLRSRowDesc* rsrow_desc, WriteBuffer* rows_data)
    : rsrow_desc_(rsrow_desc), rows_data_(rows_data), count_position_(rows_data->Position()) {
  CQLEncodeLength(0, rows_data_);
}

QLResultSet::~QLResultSet() {
}

void QLResultSet::AllocateRow() {
  ++rsrow_count_;
}

void QLResultSet::AppendColumn(const size_t index, const QLValue& value) {
  SerializeValue(
      rsrow_desc_->rscol_descs()[index].ql_type(), YQL_CLIENT_CQL, value.value(), rows_data_);
}

void QLResultSet::AppendColumn(const size_t index, const QLValuePB& value) {
  SerializeValue(rsrow_desc_->rscol_descs()[index].ql_type(), YQL_CLIENT_CQL, value, rows_data_);
}

void QLResultSet::Complete() {
  char buffer[sizeof(uint32_t)];
  NetworkByteOrder::Store32(buffer, narrow_cast<uint32_t>(rsrow_count_));
  CHECK_OK(rows_data_->Write(count_position_, buffer, sizeof(buffer)));
}

}  // namespace yb::qlexpr
