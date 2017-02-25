//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/statement.h"

namespace yb {
namespace sql {

using std::string;

StatementParameters::StatementParameters(int64_t rows_limit, const string& last_rows_result_state)
    : rows_limit_(rows_limit), last_rows_result_state_(last_rows_result_state) {
}

StatementParameters::StatementParameters(StatementParameters&& other)
    : rows_limit_(other.rows_limit_), last_rows_result_state_(move(other.last_rows_result_state_)) {
}

} // namespace sql
} // namespace yb
