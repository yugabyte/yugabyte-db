//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/common/pgsql_resultset.h"

#include "yb/common/pgsql_protocol.pb.h"
#include "yb/common/ql_value.h"
#include "yb/common/wire_protocol.h"

namespace yb {
// TODO(neil) All query classes in "yb/common" that are
// result in a number of code changes, so I'll leave this till next diff.
// namespace yqlapi

//--------------------------------------------------------------------------------------------------

PgsqlRSRowDesc::PgsqlRSRowDesc(const PgsqlRSRowDescPB& desc_pb) {
  int count = desc_pb.rscol_descs().size();
  rscol_descs_.reserve(count);
  for (auto rscol_desc_pb : desc_pb.rscol_descs()) {
    rscol_descs_.emplace_back(rscol_desc_pb.name(),
                              QLType::FromQLTypePB(rscol_desc_pb.ql_type()));
  }
}

PgsqlRSRowDesc::~PgsqlRSRowDesc() {
}

//--------------------------------------------------------------------------------------------------

PgsqlRSRow::PgsqlRSRow(int32_t rscol_count) : rscols_(rscol_count) {
}

PgsqlRSRow::~PgsqlRSRow() {
}

//--------------------------------------------------------------------------------------------------

PgsqlResultSet::PgsqlResultSet() {
}

PgsqlResultSet::~PgsqlResultSet() {
}

PgsqlRSRow *PgsqlResultSet::AllocateRSRow(int32_t rscol_count) {
  rsrows_.emplace_back(rscol_count);
  return &rsrows_.back();
}

size_t PgsqlRSRow::rscol_count() const {
  return rscols_.size();
}

const QLValue& PgsqlRSRow::rscol_value(int32_t index) const {
  return rscols_[index];
}

QLValue* PgsqlRSRow::rscol(int32_t index) {
  return &rscols_[index];
}

} // namespace yb
