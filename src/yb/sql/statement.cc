//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/statement.h"

namespace yb {
namespace sql {

using std::string;

StatementParameters::StatementParameters(int64_t rows_limit, const string& next_read_key)
    : rows_limit_(rows_limit), next_read_key_(next_read_key) {
}

StatementParameters::StatementParameters(StatementParameters&& other)
    : rows_limit_(other.rows_limit_), next_read_key_(move(other.next_read_key_)) {
}

} // namespace sql
} // namespace yb
