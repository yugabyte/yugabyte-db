// Copyright (c) YugabyteDB, Inc.
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

#include "yb/docdb/iter_util.h"

#include "yb/docdb/bounded_rocksdb_iterator.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/key_bytes.h"

#include "yb/dockv/value_type.h"
#include "yb/rocksdb/iterator.h"

#include "yb/util/bytes_formatter.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/trace.h"

// Empirically 2 is a minimal value that provides the best performance on sequential scan.
DEFINE_RUNTIME_int32(max_nexts_to_avoid_seek, 2,
    "The number of next calls to try before doing resorting to do a rocksdb seek.");

DEFINE_RUNTIME_int32(rocksdb_next_optimization_skip_threshold_percentage, 50,
    "If next instead of seek optimisation was not useful for >= "
    "rocksdb_next_optimization_not_useful_limit times and "
    "useful/num_nexts ratio is less than rocksdb_next_optimization_skip_threshold_percentage, "
    "avoid trying it further for the iterator SeekStats lifetime");
TAG_FLAG(rocksdb_next_optimization_skip_threshold_percentage, hidden);

DEFINE_RUNTIME_int32(rocksdb_next_optimization_not_useful_limit, 10,
    "See rocksdb_next_optimization_skip_threshold_percentage.");
TAG_FLAG(rocksdb_next_optimization_not_useful_limit, hidden);

// TODO(fast-backward-scan) the value is taken from FLAGS_max_nexts_to_avoid_seek default, but it
// could be not the optimal value for prev seeks and it is recommended to make some research and
// to confirm the selected value or select a different value.
DEFINE_RUNTIME_int32(max_prevs_to_avoid_seek, 2,
    "The number of prev calls to try before doing a rocksdb seek. "
    "Used by fast backward scan only.");

namespace yb::docdb {

namespace  {

inline bool IsIterAfterOrAtKey(
    const rocksdb::KeyValueEntry& entry, rocksdb::Iterator* iter, Slice key) {
  if (PREDICT_FALSE(!entry)) {
    if (PREDICT_FALSE(!iter->status().ok())) {
      VLOG(3) << "Iterator " << iter << " error: " << iter->status();
      // Caller should check Valid() after doing Seek*() and then check status() since
      // Valid() == false.
      // TODO(#16730): Add sanity check for RocksDB iterator Valid() to be checked after it is set.
    }
    return true;
  }
  return entry.key.compare(key) >= 0;
}

inline const rocksdb::KeyValueEntry& SeekPossiblyUsingNext(
    rocksdb::Iterator* iter, Slice seek_key, SeekStats& stats) {
  const auto max_nexts = FLAGS_max_nexts_to_avoid_seek;
  int nexts = max_nexts;
  while (nexts-- > 0) {
    const auto& entry = iter->Next();
    ++stats.num_nexts;
    if (IsIterAfterOrAtKey(entry, iter, seek_key)) {
      VTRACE(3, "Did $0 Next(s) instead of a Seek", stats.num_nexts);
      ++stats.num_next_optimization_useful;
      return entry;
    }
  }

  VTRACE(3, "Forced to do an actual Seek after $0 Next(s)", max_nexts);
  ++stats.num_seeks;
  ++stats.num_next_optimization_not_useful;
  return iter->Seek(seek_key);
}

inline bool IsIterBeforeKey(
    const rocksdb::KeyValueEntry& entry, rocksdb::Iterator& iter, Slice key) {
  if (PREDICT_FALSE(!entry)) {
    if (PREDICT_FALSE(!iter.status().ok())) {
      VLOG(3) << "Iterator " << &iter << " error: " << iter.status();
      // Caller should check Valid() after doing Seek*() and then check status() since
      // Valid() == false.
      // TODO(#16730): Add sanity check for RocksDB iterator Valid() to be checked after it is set.
    }
    return true;
  }
  return entry.key.compare(key) < 0;
}

inline bool SkipTryingNextInsteadOfSeek(SeekStats& stats) {
  if (stats.num_next_optimization_not_useful < FLAGS_rocksdb_next_optimization_not_useful_limit) {
    return false;
  }
  // It is only worth trying iff num_nexts * next_time < num_nexts_useful * seek_time.
  // next_optimisation_useful_ratio_threshold_percentage is configured based on some average numbers
  // for next_time / seek_time.
  //
  // Hitting this condition once will make it return true until this iterator end of life.
  return stats.num_next_optimization_useful <
         stats.num_nexts * FLAGS_rocksdb_next_optimization_skip_threshold_percentage / 100;
}

const rocksdb::KeyValueEntry& DoPerformRocksDBSeek(
    rocksdb::Iterator* iter, Slice seek_key,
    const AvoidUselessNextInsteadOfSeek avoid_useless_next_instead_of_seek, SeekStats& stats,
    const char* file_name, int line) {
  const rocksdb::KeyValueEntry* result;
  if (seek_key.size() == 0) {
    result = &iter->SeekToFirst();
    ++stats.num_seeks;
  } else if (avoid_useless_next_instead_of_seek && SkipTryingNextInsteadOfSeek(stats)) {
    VLOG_WITH_FUNC(4) << "Skip trying next instead of seek, seek_stats: " << stats.ToString();
    result = &iter->Seek(seek_key);
    ++stats.num_seeks;
  } else {
    result = &iter->Entry();
    if (!*result) {
      if (!iter->status().ok()) {
        VLOG(3) << "Iterator " << iter << " error: " << iter->status();
        // Caller should check Valid() after doing PerformRocksDBSeek() and then check status()
        // since Valid() == false.
        // TODO(#16730): Add sanity check for RocksDB iterator Valid() to be checked after it is
        // set.
        return rocksdb::KeyValueEntry::Invalid();
      }
      VLOG_WITH_FUNC(4)
          << "Seek because current iter is invalid: " << dockv::BestEffortDocDBKeyToStr(seek_key);
      result = &iter->Seek(seek_key);
      ++stats.num_seeks;
    } else {
      const auto cmp = result->key.compare(seek_key);
      if (cmp > 0) {
        VLOG_WITH_FUNC(4)
            << "Seek because position before current: " << dockv::BestEffortDocDBKeyToStr(seek_key);
        result = &iter->Seek(seek_key);
        ++stats.num_seeks;
      } else if (cmp < 0) {
        VLOG_WITH_FUNC(4) << "Seek forward: " << dockv::BestEffortDocDBKeyToStr(seek_key)
                          << ", seek_stats: " << stats.ToString();
        result = &SeekPossiblyUsingNext(iter, seek_key, stats);
      }
    }
  }
  VLOG(4) << Format(
      "PerformRocksDBSeek at $0:$1:\n"
      "    Seek key:         $2\n"
      "    Seek key (raw):   $3\n"
      "    Actual key:       $4\n"
      "    Actual key (raw): $5\n"
      "    Actual value:     $6\n"
      "    Next() calls:     $7\n"
      "    Seek() calls:     $8\n",
      file_name, line,
      dockv::BestEffortDocDBKeyToStr(seek_key),
      seek_key.ToDebugString(),
      iter->Valid()         ? dockv::BestEffortDocDBKeyToStr(iter->key())
      : iter->status().ok() ? "N/A"
                            : iter->status().ToString(),
      iter->Valid()         ? iter->key().ToDebugString()
      : iter->status().ok() ? "N/A"
                            : iter->status().ToString(),
      iter->Valid()         ? iter->value().ToDebugString()
      : iter->status().ok() ? "N/A"
                            : iter->status().ToString(),
      stats.num_nexts,
      stats.num_seeks);
  return *result;
}

} // namespace

template <typename IteratorType>
const rocksdb::KeyValueEntry& OptimizedRocksDbIterator<IteratorType>::DoSeekForward(
    Slice target, AvoidUselessNextInsteadOfSeek avoid_useless_next_instead_of_seek) {
  const auto& entry = rocksdb_iter_.Entry();
  if (IsIterAfterOrAtKey(entry, &rocksdb_iter_, target)) {
    return entry;
  }

  if (!avoid_useless_next_instead_of_seek) {
    // Do not use and do not update iterator statistics.
    SeekStats stats;
    return SeekPossiblyUsingNext(&rocksdb_iter_, target, stats);
  }

  if (SkipTryingNextInsteadOfSeek(seek_stats_)) {
    VLOG_WITH_FUNC(4) << "Skip trying next instead of seek, seek_stats: " << seek_stats_.ToString();
    ++seek_stats_.num_seeks;
    return rocksdb_iter_.Seek(target);
  }

  VLOG_WITH_FUNC(4) << "Trying next instead of seek, seek_stats: " << seek_stats_.ToString();
  return SeekPossiblyUsingNext(&rocksdb_iter_, target, seek_stats_);
}

template <typename IteratorType>
const rocksdb::KeyValueEntry& OptimizedRocksDbIterator<IteratorType>::SeekForward(Slice target) {
  return DoSeekForward(target, avoid_useless_next_instead_of_seek_);
}

template <typename IteratorType>
const rocksdb::KeyValueEntry& OptimizedRocksDbIterator<IteratorType>::SeekForward(
    const dockv::KeyBytes& key_bytes) {
  return SeekForward(key_bytes.AsSlice());
}

template <typename IteratorType>
const rocksdb::KeyValueEntry& OptimizedRocksDbIterator<IteratorType>::SeekBackward(
    Slice upper_bound_key) {
  // Check if the iterator is already positioned before the given key.
  {
    const auto& entry = rocksdb_iter_.Entry();
    if (IsIterBeforeKey(entry, rocksdb_iter_, upper_bound_key)) {
      return entry;
    }
  }

  // Try to reach the required position using Prev() calls only.
  const auto max_prevs = FLAGS_max_prevs_to_avoid_seek;
  int prevs = 0;
  while (max_prevs > prevs++) {
    const auto& entry = rocksdb_iter_.Prev();
    if (IsIterBeforeKey(entry, rocksdb_iter_, upper_bound_key)) {
      VTRACE(3, "Did $0 Prev(s) instead of a Seek", prevs);
      return entry;
    }
  }

  VTRACE(3, "Forced to do an actual Seek after $0 Prev(s)", max_prevs);
  const auto& entry = rocksdb_iter_.Seek(upper_bound_key);

  // Sanity check. It is absolutely unexpected to get and invalid entry as the iterartor is
  // positioned after the given key, which is confirmed by the above IsIterBeforeKey() call.
  DCHECK(entry.Valid()); // Maybe it's even better to put a CHECK here.
  if (PREDICT_FALSE(!entry.Valid())) {
    LOG_WITH_FUNC(DFATAL) << "Unexpected Seek() result -- invalid entry, key = '"
                          << upper_bound_key.ToDebugHexString()
                          << "', status: " << rocksdb_iter_.status();
    return entry;
  }

  // Seek() will point to the first record greater or equal to the given key, hence Prev() call
  // is required to move to a record which is less than the upper_bound_key.
  return rocksdb_iter_.Prev();
}

template <typename IteratorType>
const rocksdb::KeyValueEntry& OptimizedRocksDbIterator<IteratorType>::SeekPastSubKey(Slice key) {
  char ch = dockv::KeyEntryTypeAsChar::kHybridTime + 1;
  // Seeking past sub doc key should always try to use next instead of seek.
  return DoSeekForward(dockv::KeyBytes(key, Slice(&ch, 1)), AvoidUselessNextInsteadOfSeek::kFalse);
}

template <typename IteratorType>
const rocksdb::KeyValueEntry& OptimizedRocksDbIterator<IteratorType>::SeekOutOfSubKey(
    dockv::KeyBytes* key_bytes) {
  key_bytes->AppendKeyEntryType(dockv::KeyEntryType::kMaxByte);
  // Seeking out of sub doc key should always try to use next instead of seek.
  const auto& result = DoSeekForward(*key_bytes, AvoidUselessNextInsteadOfSeek::kFalse);
  key_bytes->RemoveKeyEntryTypeSuffix(dockv::KeyEntryType::kMaxByte);
  return result;
}

template <typename IteratorType>
const rocksdb::KeyValueEntry& OptimizedRocksDbIterator<IteratorType>::PerformRocksDBSeek(
    Slice seek_key, const char* file_name, int line) {
  return DoPerformRocksDBSeek(
      &rocksdb_iter_, seek_key, avoid_useless_next_instead_of_seek_, seek_stats_, file_name, line);
}

template class OptimizedRocksDbIterator<BoundedRocksDbIterator>;

}  // namespace yb::docdb
