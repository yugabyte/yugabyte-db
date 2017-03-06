//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/statement.h"

namespace yb {
namespace sql {

using std::string;

StatementParameters::StatementParameters(int64_t page_size, const string& paging_state)
    : page_size_(page_size) {
  CHECK(paging_state_pb_.ParseFromString(paging_state));
}

StatementParameters::StatementParameters(StatementParameters&& other)
    : page_size_(other.page_size_), paging_state_pb_(std::move(other.paging_state_pb_)) {
}

} // namespace sql
} // namespace yb
