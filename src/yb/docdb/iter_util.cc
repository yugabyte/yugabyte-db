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

#include "yb/docdb/iter_util.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/key_bytes.h"

#include "yb/rocksdb/iterator.h"

#include "yb/util/bytes_formatter.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/trace.h"

// Empirically 2 is a minimal value that provides best performance on sequential scan.
DEFINE_RUNTIME_int32(max_nexts_to_avoid_seek, 2,
    "The number of next calls to try before doing resorting to do a rocksdb seek.");

// TODO(#22373): the value is taken from FLAGS_max_nexts_to_avoid_seek default, but it could be
// not the optimal value for prev seeks and it is recommended to make some research and to confirm
// the selected value or select a different value.
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
    rocksdb::Iterator* iter, Slice seek_key, SeekStats* stats) {
  const auto max_nexts = FLAGS_max_nexts_to_avoid_seek;
  int nexts = max_nexts;
  while (nexts-- > 0) {
    const auto& entry = iter->Next();
    ++stats->next;
    if (IsIterAfterOrAtKey(entry, iter, seek_key)) {
      VTRACE(3, "Did $0 Next(s) instead of a Seek", stats->next);
      return entry;
    }
  }

  VTRACE(3, "Forced to do an actual Seek after $0 Next(s)", max_nexts);
  ++stats->seek;
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

} // namespace

const rocksdb::KeyValueEntry& SeekForward(
    const dockv::KeyBytes& key_bytes, rocksdb::Iterator *iter) {
  return SeekForward(key_bytes.AsSlice(), iter);
}

const rocksdb::KeyValueEntry& SeekPastSubKey(Slice key, rocksdb::Iterator* iter) {
  char ch = dockv::KeyEntryTypeAsChar::kHybridTime + 1;
  return SeekForward(dockv::KeyBytes(key, Slice(&ch, 1)), iter);
}

const rocksdb::KeyValueEntry& SeekOutOfSubKey(dockv::KeyBytes* key_bytes, rocksdb::Iterator* iter) {
  key_bytes->AppendKeyEntryType(dockv::KeyEntryType::kMaxByte);
  const auto& result = SeekForward(*key_bytes, iter);
  key_bytes->RemoveKeyEntryTypeSuffix(dockv::KeyEntryType::kMaxByte);
  return result;
}

SeekStats SeekPossiblyUsingNext(rocksdb::Iterator* iter, Slice seek_key) {
  SeekStats result;
  SeekPossiblyUsingNext(iter, seek_key, &result);
  return result;
}

const rocksdb::KeyValueEntry& PerformRocksDBSeek(
    rocksdb::Iterator* iter, Slice seek_key, const char* file_name, int line) {
  SeekStats stats;
  const rocksdb::KeyValueEntry* result;
  if (seek_key.size() == 0) {
    result = &iter->SeekToFirst();
    ++stats.seek;
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
      ++stats.seek;
    } else {
      const auto cmp = result->key.compare(seek_key);
      if (cmp > 0) {
        VLOG_WITH_FUNC(4)
            << "Seek because position after current: " << dockv::BestEffortDocDBKeyToStr(seek_key);
        result = &iter->Seek(seek_key);
        ++stats.seek;
      } else if (cmp < 0) {
        VLOG_WITH_FUNC(4)
            << "Seek forward: " << dockv::BestEffortDocDBKeyToStr(seek_key);
        result = &SeekPossiblyUsingNext(iter, seek_key, &stats);
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
      FormatSliceAsStr(seek_key),
      iter->Valid()         ? dockv::BestEffortDocDBKeyToStr(iter->key())
      : iter->status().ok() ? "N/A"
                            : iter->status().ToString(),
      iter->Valid()         ? FormatSliceAsStr(iter->key())
      : iter->status().ok() ? "N/A"
                            : iter->status().ToString(),
      iter->Valid()         ? FormatSliceAsStr(iter->value())
      : iter->status().ok() ? "N/A"
                            : iter->status().ToString(),
      stats.next,
      stats.seek);
  return *result;
}

const rocksdb::KeyValueEntry& SeekForward(Slice slice, rocksdb::Iterator *iter) {
  const auto& entry = iter->Entry();
  if (IsIterAfterOrAtKey(entry, iter, slice)) {
    return entry;
  }

  SeekStats stats;
  return SeekPossiblyUsingNext(iter, slice, &stats);
}

const rocksdb::KeyValueEntry& SeekBackward(Slice upper_bound_key, rocksdb::Iterator& iter) {
  // Check if the iterator is already positioned before the given key.
  {
    const auto& entry = iter.Entry();
    if (IsIterBeforeKey(entry, iter, upper_bound_key)) {
      return entry;
    }
  }

  // Try to reach the required position using Prev() calls only.
  const auto max_prevs = FLAGS_max_prevs_to_avoid_seek;
  int prevs = 0;
  while (max_prevs > prevs++) {
    const auto& entry = iter.Prev();
    if (IsIterBeforeKey(entry, iter, upper_bound_key)) {
      VTRACE(3, "Did $0 Prev(s) instead of a Seek", prevs);
      return entry;
    }
  }

  VTRACE(3, "Forced to do an actual Seek after $0 Prev(s)", max_prevs);
  const auto& entry = iter.Seek(upper_bound_key);

  // Sanity check. It is absolutely unexpected to get and invalid entry as the iterartor is
  // positioned after the given key, which is confirmed by the above IsIterBeforeKey() call.
  DCHECK(entry.Valid()); // Maybe it's even better to put a CHECK here.
  if (PREDICT_FALSE(!entry.Valid())) {
    LOG_WITH_FUNC(ERROR) << "Unexpected Seek() result -- invalid entry, key = '"
                         << upper_bound_key.ToDebugHexString() << "', status: " << iter.status();
    return entry;
  }

  // Seek() will point to the first record greater or equal to the given key, hence Prev() call
  // is required to move to a record which is less than the upper_bound_key.
  return iter.Prev();
}

}  // namespace yb::docdb
