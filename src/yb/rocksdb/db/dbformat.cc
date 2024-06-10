//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
#include "yb/rocksdb/db/dbformat.h"

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include <inttypes.h>
#include <stdio.h>

#include "yb/util/logging.h"
#include "yb/util/result.h"

#include "yb/rocksdb/port/port.h"
#include "yb/rocksdb/util/coding.h"
#include "yb/rocksdb/util/logging.h"
#include "yb/rocksdb/util/perf_context_imp.h"

namespace rocksdb {

uint64_t PackSequenceAndType(uint64_t seq, ValueType t) {
  assert(seq <= kMaxSequenceNumber);
  assert(IsValueType(t));
  return (seq << 8) | t;
}

SequenceAndType UnPackSequenceAndTypeFromEnd(const void* end) {
  uint64_t packed;
  memcpy(&packed, static_cast<const char*>(end) - kLastInternalComponentSize, sizeof(packed));
  auto result = SequenceAndType {
    .sequence = packed >> 8,
    .type = static_cast<ValueType>(packed & 0xff),
  };

  assert(result.sequence <= kMaxSequenceNumber);
  assert(IsValueType(result.type));

  return result;
}

void AppendInternalKey(std::string* result, const ParsedInternalKey& key) {
  result->append(key.user_key.cdata(), key.user_key.size());
  PutFixed64(result, PackSequenceAndType(key.sequence, key.type));
}

std::string ParsedInternalKey::DebugString(bool hex) const {
  char buf[50];
  snprintf(buf, sizeof(buf), "' @ %" PRIu64 ": %d", sequence,
           static_cast<int>(type));
  std::string result = "'";
  result += user_key.ToString(hex);
  result += buf;
  return result;
}

std::string InternalKey::DebugString(const std::string& rep, bool hex) {
  std::string result;
  ParsedInternalKey parsed;
  if (ParseInternalKey(rep, &parsed)) {
    result = parsed.DebugString(hex);
  } else {
    result = "(bad)";
    result.append(EscapeString(rep));
  }
  return result;
}

const char* InternalKeyComparator::Name() const {
  return name_.c_str();
}

int InternalKeyComparator::Compare(Slice akey, Slice bkey) const {
  // Order by:
  //    increasing user key (according to user-supplied comparator)
  //    decreasing sequence number
  //    decreasing type (though sequence# should be enough to disambiguate)
  DCHECK_GE(akey.size(), kLastInternalComponentSize);
  auto user_key_end_a = akey.end() - kLastInternalComponentSize;
  DCHECK_GE(bkey.size(), kLastInternalComponentSize);
  auto user_key_end_b = bkey.end() - kLastInternalComponentSize;
  int r = user_comparator_->Compare(
      Slice(akey.data(), user_key_end_a), Slice(bkey.data(), user_key_end_b));
  PERF_COUNTER_ADD(user_key_comparison_count, 1);
  if (r == 0) {
    const uint64_t anum = DecodeFixed64(user_key_end_a);
    const uint64_t bnum = DecodeFixed64(user_key_end_b);
    if (anum > bnum) {
      r = -1;
    } else if (anum < bnum) {
      r = +1;
    }
  }
  return r;
}

int InternalKeyComparator::Compare(const ParsedInternalKey& a,
                                   const ParsedInternalKey& b) const {
  // Order by:
  //    increasing user key (according to user-supplied comparator)
  //    decreasing sequence number
  //    decreasing type (though sequence# should be enough to disambiguate)
  int r = user_comparator_->Compare(a.user_key, b.user_key);
  PERF_COUNTER_ADD(user_key_comparison_count, 1);
  if (r == 0) {
    if (a.sequence > b.sequence) {
      r = -1;
    } else if (a.sequence < b.sequence) {
      r = +1;
    } else if (a.type > b.type) {
      r = -1;
    } else if (a.type < b.type) {
      r = +1;
    }
  }
  return r;
}

void InternalKeyComparator::FindShortestSeparator(
      std::string* start,
      const Slice& limit) const {
  // Attempt to shorten the user portion of the key
  Slice user_start = ExtractUserKey(*start);
  Slice user_limit = ExtractUserKey(limit);
  std::string tmp = user_start.ToBuffer();
  user_comparator_->FindShortestSeparator(&tmp, user_limit);
  if (tmp.size() < user_start.size() &&
      user_comparator_->Compare(user_start, tmp) < 0) {
    // User key has become shorter physically, but larger logically.
    // Tack on the earliest possible number to the shortened user key.
    PutFixed64(&tmp, PackSequenceAndType(kMaxSequenceNumber, kValueTypeForSeek));
    assert(this->Compare(*start, tmp) < 0);
    assert(this->Compare(tmp, limit) < 0);
    start->swap(tmp);
  }
}

void InternalKeyComparator::FindShortSuccessor(std::string* key) const {
  Slice user_key = ExtractUserKey(*key);
  std::string tmp = user_key.ToBuffer();
  user_comparator_->FindShortSuccessor(&tmp);
  if (tmp.size() < user_key.size() &&
      user_comparator_->Compare(user_key, tmp) < 0) {
    // User key has become shorter physically, but larger logically.
    // Tack on the earliest possible number to the shortened user key.
    PutFixed64(&tmp, PackSequenceAndType(kMaxSequenceNumber, kValueTypeForSeek));
    assert(this->Compare(*key, tmp) < 0);
    key->swap(tmp);
  }
}

LookupKey::LookupKey(const Slice& _user_key, SequenceNumber s) {
  size_t usize = _user_key.size();
  size_t needed = usize + 13;  // A conservative estimate
  char* dst;
  if (needed <= sizeof(space_)) {
    dst = space_;
  } else {
    dst = new char[needed];
  }
  start_ = dst;
  // NOTE: We don't support users keys of more than 2GB :)
  dst = EncodeVarint32(dst, static_cast<uint32_t>(usize + 8));
  kstart_ = dst;
  memcpy(dst, _user_key.data(), usize);
  dst += usize;
  EncodeFixed64(dst, PackSequenceAndType(s, kValueTypeForSeek));
  dst += 8;
  end_ = dst;
}

}  // namespace rocksdb
