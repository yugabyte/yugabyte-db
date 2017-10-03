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

#include "kudu/tablet/delta_iterator_merger.h"

#include <algorithm>

#include "kudu/gutil/strings/join.h"
#include "kudu/gutil/strings/strcat.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/tablet/deltafile.h"

namespace kudu {
namespace tablet {

using std::shared_ptr;
using std::string;
using std::vector;
using strings::Substitute;

DeltaIteratorMerger::DeltaIteratorMerger(
    vector<shared_ptr<DeltaIterator> > iters)
    : iters_(std::move(iters)) {}

Status DeltaIteratorMerger::Init(ScanSpec *spec) {
  for (const shared_ptr<DeltaIterator> &iter : iters_) {
    RETURN_NOT_OK(iter->Init(spec));
  }
  return Status::OK();
}

Status DeltaIteratorMerger::SeekToOrdinal(rowid_t idx) {
  for (const shared_ptr<DeltaIterator> &iter : iters_) {
    RETURN_NOT_OK(iter->SeekToOrdinal(idx));
  }
  return Status::OK();
}

Status DeltaIteratorMerger::PrepareBatch(size_t nrows, PrepareFlag flag) {
  for (const shared_ptr<DeltaIterator> &iter : iters_) {
    RETURN_NOT_OK(iter->PrepareBatch(nrows, flag));
  }
  return Status::OK();
}

Status DeltaIteratorMerger::ApplyUpdates(size_t col_to_apply, ColumnBlock *dst) {
  for (const shared_ptr<DeltaIterator> &iter : iters_) {
    RETURN_NOT_OK(iter->ApplyUpdates(col_to_apply, dst));
  }
  return Status::OK();
}

Status DeltaIteratorMerger::ApplyDeletes(SelectionVector *sel_vec) {
  for (const shared_ptr<DeltaIterator> &iter : iters_) {
    RETURN_NOT_OK(iter->ApplyDeletes(sel_vec));
  }
  return Status::OK();
}

Status DeltaIteratorMerger::CollectMutations(vector<Mutation *> *dst, Arena *arena) {
  for (const shared_ptr<DeltaIterator> &iter : iters_) {
    RETURN_NOT_OK(iter->CollectMutations(dst, arena));
  }
  // TODO: do we need to do some kind of sorting here to deal with out-of-order
  // timestamps?
  return Status::OK();
}

struct DeltaKeyUpdateComparator {
  bool operator() (const DeltaKeyAndUpdate& a, const DeltaKeyAndUpdate &b) {
    return a.key.CompareTo<REDO>(b.key) < 0;
  }
};

Status DeltaIteratorMerger::FilterColumnIdsAndCollectDeltas(
    const vector<ColumnId>& col_ids,
    vector<DeltaKeyAndUpdate>* out,
    Arena* arena) {
  for (const shared_ptr<DeltaIterator>& iter : iters_) {
    RETURN_NOT_OK(iter->FilterColumnIdsAndCollectDeltas(col_ids, out, arena));
  }
  // We use a stable sort here since an input may include multiple deltas for the
  // same row at the same timestamp, in the case of a user batch which had several
  // mutations for the same row. Stable sort preserves the user-provided ordering.
  std::stable_sort(out->begin(), out->end(), DeltaKeyUpdateComparator());
  return Status::OK();
}

bool DeltaIteratorMerger::HasNext() {
  for (const shared_ptr<DeltaIterator>& iter : iters_) {
    if (iter->HasNext()) {
      return true;
    }
  }

  return false;
}

string DeltaIteratorMerger::ToString() const {
  string ret;
  ret.append("DeltaIteratorMerger(");

  bool first = true;
  for (const shared_ptr<DeltaIterator> &iter : iters_) {
    if (!first) {
      ret.append(", ");
    }
    first = false;

    ret.append(iter->ToString());
  }
  ret.append(")");
  return ret;
}


Status DeltaIteratorMerger::Create(
    const vector<shared_ptr<DeltaStore> > &stores,
    const Schema* projection,
    const MvccSnapshot &snapshot,
    shared_ptr<DeltaIterator>* out) {
  vector<shared_ptr<DeltaIterator> > delta_iters;

  for (const shared_ptr<DeltaStore> &store : stores) {
    DeltaIterator* raw_iter;
    Status s = store->NewDeltaIterator(projection, snapshot, &raw_iter);
    if (s.IsNotFound()) {
      continue;
    }
    RETURN_NOT_OK_PREPEND(s, Substitute("Could not create iterator for store $0",
                                        store->ToString()));

    delta_iters.push_back(shared_ptr<DeltaIterator>(raw_iter));
  }

  if (delta_iters.size() == 1) {
    // If we only have one input to the "merge", we can just directly
    // return that iterator.
    *out = delta_iters[0];
  } else {
    *out = shared_ptr<DeltaIterator>(new DeltaIteratorMerger(delta_iters));
  }
  return Status::OK();
}

} // namespace tablet
} // namespace kudu
