// Copyright (c) Yugabyte, Inc.
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

#include "yb/rocksdb/db/db_iterator_wrapper.h"

#include "yb/util/bytes_formatter.h"
#include "yb/util/logging.h"

using yb::Format;

namespace rocksdb {

template<typename Functor>
void TransitionLoggingIteratorWrapper::LogBeforeAndAfter(
    const std::string& action_str, const Functor& action) {
  std::string before = StateStr();
  action();
  std::string after = StateStr();
  if (before == after) {
    LOG_WITH_PREFIX(INFO) << action_str << ": state not changed: " << before;
  } else {
    LOG_WITH_PREFIX(INFO) << action_str << ": before=" << before << ", after=" << after;
  }
}

const KeyValueEntry& TransitionLoggingIteratorWrapper::SeekToFirst() {
  LogBeforeAndAfter(__func__, [this]() { DBIteratorWrapper::SeekToFirst(); });
  return Entry();
}

const KeyValueEntry& TransitionLoggingIteratorWrapper::SeekToLast() {
  LogBeforeAndAfter(__func__, [this]() { DBIteratorWrapper::SeekToLast(); });
  return Entry();
}

const KeyValueEntry& TransitionLoggingIteratorWrapper::Seek(Slice target) {
  LogBeforeAndAfter(
      Format("Seek($0)", target.ToDebugString()),
      [this, target]() { DBIteratorWrapper::Seek(target); });
  return Entry();
}

const KeyValueEntry& TransitionLoggingIteratorWrapper::Next() {
  LogBeforeAndAfter(__func__, [this]() { DBIteratorWrapper::Next(); });
  return Entry();
}

const KeyValueEntry& TransitionLoggingIteratorWrapper::Prev() {
  LogBeforeAndAfter(__func__, [this]() { DBIteratorWrapper::Prev(); });
  return Entry();
}

std::string TransitionLoggingIteratorWrapper::LogPrefix() const {
  return StringPrintf("%sIter %p ", rocksdb_log_prefix_.c_str(), wrapped_.get());
}

std::string TransitionLoggingIteratorWrapper::StateStr() const {
  if (!Valid()) {
    return "<Invalid>";
  }
  return Format("{ key: $0 value $1 }", key().ToDebugString(), value().ToDebugString());
}

}  // namespace rocksdb
