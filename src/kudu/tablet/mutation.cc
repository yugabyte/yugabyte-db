// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "kudu/gutil/atomicops.h"
#include "kudu/gutil/strings/strcat.h"
#include "kudu/tablet/mutation.h"
#include <string>

namespace kudu {
namespace tablet {

string Mutation::StringifyMutationList(const Schema &schema, const Mutation *head) {
  string ret;

  ret.append("[");

  bool first = true;
  while (head != nullptr) {
    if (!first) {
      ret.append(", ");
    }
    first = false;

    StrAppend(&ret, "@", head->timestamp().ToString(), "(");
    ret.append(head->changelist().ToString(schema));
    ret.append(")");

    head = head->next();
  }

  ret.append("]");
  return ret;
}


void Mutation::AppendToListAtomic(Mutation **list) {
  DoAppendToList<true>(list);
}

void Mutation::AppendToList(Mutation **list) {
  DoAppendToList<false>(list);
}

namespace {
template<bool ATOMIC>
inline void Store(Mutation** pointer, Mutation* val);

template<>
inline void Store<true>(Mutation** pointer, Mutation* val) {
  Release_Store(reinterpret_cast<AtomicWord*>(pointer),
                reinterpret_cast<AtomicWord>(val));
}

template<>
inline void Store<false>(Mutation** pointer, Mutation* val) {
  *pointer = val;
}
} // anonymous namespace

template<bool ATOMIC>
inline void Mutation::DoAppendToList(Mutation **list) {
  next_ = nullptr;
  if (*list == nullptr) {
    Store<ATOMIC>(list, this);
  } else {
    // Find tail and append.
    Mutation *tail = *list;
    while (tail->next_ != nullptr) {
      tail = tail->next_;
    }
    Store<ATOMIC>(&tail->next_, this);
  }
}

} // namespace tablet
} // namespace kudu
