//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/util/statement_params.h"

#include <glog/logging.h>

namespace yb {
namespace sql {

using std::string;

StatementParameters::StatementParameters() : page_size_(INT64_MAX) {
}

StatementParameters::~StatementParameters() {
}

} // namespace sql
} // namespace yb
