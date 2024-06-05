// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#pragma once

#include "yb/common/doc_hybrid_time.h"
#include "yb/common/read_hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/util/enums.h"
#include "yb/util/slice.h"
#include "yb/util/slice_parts.h"

DECLARE_bool(dump_transactions);

namespace yb {
namespace docdb {

YB_DEFINE_ENUM(TransactionDumpOp,
               ((kApplyIntent, 1)) // Apply intents at participant.
               ((kRead, 2)) // Any read.
               ((kCommit, 3)) // Transaction committed.
               ((kStatus, 4)) // Transaction status received.
               ((kConflicts, 5)) // Transaction conflicts.
               ((kApplied, 6)) // Transaction applied on all involved tablets.
               ((kRemove, 7)) // Transaction removed from participant.
               ((kRemoveIntent, 8)) // Remove intents at participant.
               );

void TransactionDump(const SliceParts& parts);

template <class T>
struct DumpToSliceAsPod : public std::is_pod<T> {};

template <>
struct DumpToSliceAsPod<DocHybridTime> : public std::integral_constant<bool, true> {};

template <>
struct DumpToSliceAsPod<HybridTime> : public std::integral_constant<bool, true> {};

template <>
struct DumpToSliceAsPod<ReadHybridTime> : public std::integral_constant<bool, true> {};

template <class T>
typename std::enable_if<DumpToSliceAsPod<T>::value, Slice>::type ToDumpSlice(const T& value) {
  return Slice(pointer_cast<const char*>(&value), sizeof(value));
}

inline Slice ToDumpSlice(const TransactionId& value) {
  return Slice(value.data(), value.size());
}

inline Slice ToDumpSlice(Slice slice) {
  return slice;
}

template<class... Args>
void TransactionDump(TransactionDumpOp op, Args&&... args) {
  size_t size = 0;
  std::array<Slice, 2 + sizeof...(Args)> array = {
    Slice(pointer_cast<const char*>(&size), sizeof(size)),
    Slice(pointer_cast<const char*>(&op), 1),
    ToDumpSlice(args)...
  };
  for (size_t i = 1; i != array.size(); ++i) {
    size += array[i].size();
  }
  SliceParts parts(array);
  TransactionDump(parts);
}

#define YB_TRANSACTION_DUMP(op, ...) do { \
    if (FLAGS_dump_transactions) { \
      TransactionDump(::yb::docdb::TransactionDumpOp::BOOST_PP_CAT(k, op), __VA_ARGS__); \
    } \
  } while (false)

} // namespace docdb
} // namespace yb
