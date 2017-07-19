//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/common/ql_resultset.h"
#include "yb/common/wire_protocol.h"

namespace yb {
// TODO(neil) All QL classes in "yb/common" needs to be group under a namespace. Doing that would
// result in a number of code changes, so I'll leave this till next diff.
// namespace yqlapi

//--------------------------------------------------------------------------------------------------

QLRSRowDesc::QLRSRowDesc(const QLRSRowDescPB& desc_pb) {
  int count = desc_pb.rscol_descs().size();
  rscol_descs_.reserve(count);
  for (auto rscol_desc_pb : desc_pb.rscol_descs()) {
    rscol_descs_.emplace_back(rscol_desc_pb.name(),
                              QLType::FromQLTypePB(rscol_desc_pb.ql_type()));
  }
}

QLRSRowDesc::~QLRSRowDesc() {
}

//--------------------------------------------------------------------------------------------------

QLRSRow::QLRSRow(int32_t rscol_count) : rscols_(rscol_count) {
}

QLRSRow::~QLRSRow() {
}

CHECKED_STATUS QLRSRow::CQLSerialize(const QLClient& client,
                                      const QLRSRowDesc& rsrow_desc,
                                      faststring* buffer) const {
  int count = rsrow_desc.rscol_count();
  DCHECK_EQ(count, rscol_count()) << "Wrong count of fields in result set";

  int idx = 0;
  for (auto rscol_desc : rsrow_desc.rscol_descs()) {
    rscols_[idx].Serialize(rscol_desc.ql_type(), client, buffer);
    idx++;
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

QLResultSet::QLResultSet() {
}

QLResultSet::~QLResultSet() {
}

QLRSRow *QLResultSet::AllocateRSRow(int32_t rscol_count) {
  rsrows_.emplace_back(rscol_count);
  return &rsrows_.back();
}

CHECKED_STATUS QLResultSet::CQLSerialize(const QLClient& client,
                                         const QLRSRowDesc& rsrow_desc,
                                         faststring* buffer) const {
  CQLEncodeLength(rsrows_.size(), buffer);
  for (const auto& rsrow : rsrows_) {
    RETURN_NOT_OK(rsrow.CQLSerialize(client, rsrow_desc, buffer));
  }
  return Status::OK();
}

} // namespace yb
