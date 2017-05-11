//
// Copyright (c) YugaByte, Inc.
//

#ifndef YB_UTIL_RESULT_H
#define YB_UTIL_RESULT_H

#include <boost/variant.hpp>

#include "rocksdb/status.h" // TODO: switch to single status

namespace yb {

template<class Value>
using Result = boost::variant<rocksdb::Status, Value>;

template<class Value>
const rocksdb::Status* status(const Result<Value>& result) {
  return boost::get<rocksdb::Status>(&result);
}

template<class Value>
const Value* value(const Result<Value>& result) {
  return boost::get<Value>(&result);
}

template<class Value>
const Value* value(Result<Value>* result) {
  return boost::get<Value>(result);
}

} // namespace yb

#endif // YB_UTIL_RESULT_H
