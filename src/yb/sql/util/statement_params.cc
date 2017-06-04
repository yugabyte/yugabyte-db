//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/util/statement_params.h"

#include <glog/logging.h>

namespace yb {
namespace sql {

using std::string;

StatementParameters::StatementParameters()
  : page_size_(INT64_MAX),
    yb_consistency_level_(YBConsistencyLevel::STRONG) {
}

StatementParameters::StatementParameters(const StatementParameters& other)
  : page_size_(other.page_size_),
    paging_state_(
      other.paging_state_ != nullptr ? new YQLPagingStatePB(*other.paging_state_) : nullptr),
    yb_consistency_level_(YBConsistencyLevel::STRONG) {
}

StatementParameters::~StatementParameters() {
}

} // namespace sql
} // namespace yb
