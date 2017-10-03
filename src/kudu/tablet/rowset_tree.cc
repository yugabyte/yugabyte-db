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

#include "kudu/tablet/rowset_tree.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "kudu/gutil/stl_util.h"
#include "kudu/tablet/rowset.h"
#include "kudu/util/interval_tree.h"
#include "kudu/util/interval_tree-inl.h"
#include "kudu/util/slice.h"

using std::vector;
using std::shared_ptr;

namespace kudu {
namespace tablet {

namespace {

// Lexicographic, first by slice, then by rowset pointer, then by start/stop
bool RSEndpointBySliceCompare(const RowSetTree::RSEndpoint& a,
                              const RowSetTree::RSEndpoint& b) {
  int slice_cmp = a.slice_.compare(b.slice_);
  if (slice_cmp) return slice_cmp < 0;
  ptrdiff_t rs_cmp = a.rowset_ - b.rowset_;
  if (rs_cmp) return rs_cmp < 0;
  if (a.endpoint_ != b.endpoint_) return a.endpoint_ == RowSetTree::START;
  return false;
}

} // anonymous namespace

// Entry for use in the interval tree.
struct RowSetWithBounds {
  RowSet *rowset;
  string min_key;
  string max_key;
};

// Traits struct for IntervalTree.
struct RowSetIntervalTraits {
  typedef Slice point_type;
  typedef RowSetWithBounds *interval_type;

  static Slice get_left(const RowSetWithBounds *rs) {
    return Slice(rs->min_key);
  }

  static Slice get_right(const RowSetWithBounds *rs) {
    return Slice(rs->max_key);
  }

  static int compare(const Slice &a, const Slice &b) {
    return a.compare(b);
  }
};

RowSetTree::RowSetTree()
  : initted_(false) {
}

Status RowSetTree::Reset(const RowSetVector &rowsets) {
  CHECK(!initted_);
  std::vector<RowSetWithBounds *> entries;
  RowSetVector unbounded;
  ElementDeleter deleter(&entries);
  entries.reserve(rowsets.size());
  std::vector<RSEndpoint> endpoints;
  endpoints.reserve(rowsets.size()*2);

  // Iterate over each of the provided RowSets, fetching their
  // bounds and adding them to the local vectors.
  for (const shared_ptr<RowSet> &rs : rowsets) {
    gscoped_ptr<RowSetWithBounds> rsit(new RowSetWithBounds());
    rsit->rowset = rs.get();
    Slice min_key, max_key;
    Status s = rs->GetBounds(&min_key, &max_key);
    if (s.IsNotSupported()) {
      // This rowset is a MemRowSet, for which the bounds change as more
      // data gets inserted. Therefore we can't put it in the static
      // interval tree -- instead put it on the list which is consulted
      // on every access.
      unbounded.push_back(rs);
      continue;
    } else if (!s.ok()) {
      LOG(WARNING) << "Unable to construct RowSetTree: "
                   << rs->ToString() << " unable to determine its bounds: "
                   << s.ToString();
      return s;
    }
    DCHECK_LE(min_key.compare(max_key), 0)
      << "Rowset min must be <= max: " << rs->ToString();
    // Load into key endpoints.
    endpoints.push_back(RSEndpoint(rsit->rowset, START, min_key));
    endpoints.push_back(RSEndpoint(rsit->rowset, STOP, max_key));

    // Load bounds and save entry
    rsit->min_key = min_key.ToString();
    rsit->max_key = max_key.ToString();
    entries.push_back(rsit.release());
  }

  // Sort endpoints
  std::sort(endpoints.begin(), endpoints.end(), RSEndpointBySliceCompare);

  // Install the vectors into the object.
  entries_.swap(entries);
  unbounded_rowsets_.swap(unbounded);
  tree_.reset(new IntervalTree<RowSetIntervalTraits>(entries_));
  key_endpoints_.swap(endpoints);
  all_rowsets_.assign(rowsets.begin(), rowsets.end());
  initted_ = true;

  return Status::OK();
}

void RowSetTree::FindRowSetsIntersectingInterval(const Slice &lower_bound,
                                                 const Slice &upper_bound,
                                                 vector<RowSet *> *rowsets) const {
  DCHECK(initted_);

  // All rowsets with unknown bounds need to be checked.
  for (const shared_ptr<RowSet> &rs : unbounded_rowsets_) {
    rowsets->push_back(rs.get());
  }

  // perf TODO: make it possible to query using raw Slices
  // instead of copying to strings here
  RowSetWithBounds query;
  query.min_key = lower_bound.ToString();
  query.max_key = upper_bound.ToString();

  vector<RowSetWithBounds *> from_tree;
  from_tree.reserve(all_rowsets_.size());
  tree_->FindIntersectingInterval(&query, &from_tree);
  rowsets->reserve(rowsets->size() + from_tree.size());
  for (RowSetWithBounds *rs : from_tree) {
    rowsets->push_back(rs->rowset);
  }
}

void RowSetTree::FindRowSetsWithKeyInRange(const Slice &encoded_key,
                                           vector<RowSet *> *rowsets) const {
  DCHECK(initted_);

  // All rowsets with unknown bounds need to be checked.
  for (const shared_ptr<RowSet> &rs : unbounded_rowsets_) {
    rowsets->push_back(rs.get());
  }

  // Query the interval tree to efficiently find rowsets with known bounds
  // whose ranges overlap the probe key.
  vector<RowSetWithBounds *> from_tree;
  from_tree.reserve(all_rowsets_.size());
  tree_->FindContainingPoint(encoded_key, &from_tree);
  rowsets->reserve(rowsets->size() + from_tree.size());
  for (RowSetWithBounds *rs : from_tree) {
    rowsets->push_back(rs->rowset);
  }
}

RowSetTree::~RowSetTree() {
  STLDeleteElements(&entries_);
}

} // namespace tablet
} // namespace kudu
