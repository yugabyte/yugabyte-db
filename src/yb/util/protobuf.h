// Copyright (c) YugaByte, Inc.

#ifndef YB_UTIL_PROTOBUF_H_
#define YB_UTIL_PROTOBUF_H_

namespace yb {
namespace util {

template<class T>
google::protobuf::RepeatedPtrField<T> ToRepeatedPtrField(const vector<T> &input) {
  google::protobuf::RepeatedPtrField<T> elements;
  elements.Reserve(input.size());
  for (const auto& elem : input) {
    *(elements.Add()) = elem;
  }
  return elements;
}

}  // namespace util
}  // namespace yb

#endif  // YB_UTIL_PROTOBUF_H_
