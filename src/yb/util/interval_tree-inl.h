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
#ifndef KUDU_UTIL_INTERVAL_TREE_INL_H
#define KUDU_UTIL_INTERVAL_TREE_INL_H

#include <algorithm>
#include <vector>

namespace kudu {

template<class Traits>
IntervalTree<Traits>::IntervalTree(const IntervalVector &intervals)
  : root_(NULL) {
  if (!intervals.empty()) {
    root_ = CreateNode(intervals);
  }
}

template<class Traits>
IntervalTree<Traits>::~IntervalTree() {
  delete root_;
}

template<class Traits>
void IntervalTree<Traits>::FindContainingPoint(const point_type &query,
                                               IntervalVector *results) const {
  if (root_) {
    root_->FindContainingPoint(query, results);
  }
}

template<class Traits>
void IntervalTree<Traits>::FindIntersectingInterval(const interval_type &query,
                                                    IntervalVector *results) const {
  if (root_) {
    root_->FindIntersectingInterval(query, results);
  }
}

template<class Traits>
static bool LessThan(const typename Traits::point_type &a,
                     const typename Traits::point_type &b) {
  return Traits::compare(a, b) < 0;
}

// Select a split point which attempts to evenly divide 'in' into three groups:
//  (a) those that are fully left of the split point
//  (b) those that overlap the split point.
//  (c) those that are fully right of the split point
// These three groups are stored in the output parameters '*left', '*overlapping',
// and '*right', respectively. The selected split point is stored in *split_point.
//
// For example, the input interval set:
//
//   |------1-------|         |-----2-----|
//       |--3--|    |---4--|    |----5----|
//                     |
// Resulting split:    | Partition point
//                     |
//
// *left: intervals 1 and 3
// *overlapping: interval 4
// *right: intervals 2 and 5
template<class Traits>
void IntervalTree<Traits>::Partition(const IntervalVector &in,
                                     point_type *split_point,
                                     IntervalVector *left,
                                     IntervalVector *overlapping,
                                     IntervalVector *right) {
  CHECK(!in.empty());

  // Pick a split point which is the median of all of the interval boundaries.
  std::vector<point_type> endpoints;
  endpoints.reserve(in.size() * 2);
  for (const interval_type &interval : in) {
    endpoints.push_back(Traits::get_left(interval));
    endpoints.push_back(Traits::get_right(interval));
  }
  std::sort(endpoints.begin(), endpoints.end(), LessThan<Traits>);
  *split_point = endpoints[endpoints.size() / 2];

  // Partition into the groups based on the determined split point.
  for (const interval_type &interval : in) {
    if (Traits::compare(Traits::get_right(interval), *split_point) < 0) {
      //                 | split point
      // |------------|  |
      //    interval
      left->push_back(interval);
    } else if (Traits::compare(Traits::get_left(interval), *split_point) > 0) {
      //                 | split point
      //                 |    |------------|
      //                         interval
      right->push_back(interval);
    } else {
      //                 | split point
      //                 |
      //          |------------|
      //             interval
      overlapping->push_back(interval);
    }
  }
}

template<class Traits>
typename IntervalTree<Traits>::node_type *IntervalTree<Traits>::CreateNode(
  const IntervalVector &intervals) {
  IntervalVector left, right, overlap;
  point_type split_point;

  // First partition the input intervals and select a split point
  Partition(intervals, &split_point, &left, &overlap, &right);

  // Recursively subdivide the intervals which are fully left or fully
  // right of the split point into subtree nodes.
  node_type *left_node = !left.empty() ? CreateNode(left) : NULL;
  node_type *right_node = !right.empty() ? CreateNode(right) : NULL;

  return new node_type(split_point, left_node, overlap, right_node);
}

namespace interval_tree_internal {

// Node in the interval tree.
template<typename Traits>
class ITNode {
 private:
  // Import types.
  typedef std::vector<typename Traits::interval_type> IntervalVector;
  typedef typename Traits::interval_type interval_type;
  typedef typename Traits::point_type point_type;

 public:
  ITNode(point_type split_point,
         ITNode<Traits> *left,
         const IntervalVector &overlap,
         ITNode<Traits> *right);
  ~ITNode();

  // See IntervalTree::FindContainingPoint(...)
  void FindContainingPoint(const point_type &query,
                           IntervalVector *results) const;

  // See IntervalTree::FindIntersectingInterval(...)
  void FindIntersectingInterval(const interval_type &query,
                                IntervalVector *results) const;

 private:
  // Comparators for sorting lists of intervals.
  static bool SortByAscLeft(const interval_type &a, const interval_type &b);
  static bool SortByDescRight(const interval_type &a, const interval_type &b);

  // Partition point of this node.
  point_type split_point_;

  // Those nodes that overlap with split_point_, in ascending order by their left side.
  IntervalVector overlapping_by_asc_left_;

  // Those nodes that overlap with split_point_, in descending order by their right side.
  IntervalVector overlapping_by_desc_right_;

  // Tree node for intervals fully left of split_point_, or NULL.
  ITNode *left_;

  // Tree node for intervals fully right of split_point_, or NULL.
  ITNode *right_;

  DISALLOW_COPY_AND_ASSIGN(ITNode);
};

template<class Traits>
bool ITNode<Traits>::SortByAscLeft(const interval_type &a, const interval_type &b) {
  return Traits::compare(Traits::get_left(a), Traits::get_left(b)) < 0;
}

template<class Traits>
bool ITNode<Traits>::SortByDescRight(const interval_type &a, const interval_type &b) {
  return Traits::compare(Traits::get_right(a), Traits::get_right(b)) > 0;
}

template <class Traits>
ITNode<Traits>::ITNode(typename Traits::point_type split_point,
                       ITNode<Traits> *left, const IntervalVector &overlap,
                       ITNode<Traits> *right)
    : split_point_(std::move(split_point)), left_(left), right_(right) {
  // Store two copies of the set of intervals which overlap the split point:
  // 1) Sorted by ascending left boundary
  overlapping_by_asc_left_.assign(overlap.begin(), overlap.end());
  std::sort(overlapping_by_asc_left_.begin(), overlapping_by_asc_left_.end(), SortByAscLeft);
  // 2) Sorted by descending right boundary
  overlapping_by_desc_right_.assign(overlap.begin(), overlap.end());
  std::sort(overlapping_by_desc_right_.begin(), overlapping_by_desc_right_.end(), SortByDescRight);
}

template<class Traits>
ITNode<Traits>::~ITNode() {
  if (left_) delete left_;
  if (right_) delete right_;
}

template<class Traits>
void ITNode<Traits>::FindContainingPoint(const point_type &query,
                                         IntervalVector *results) const {
  int cmp = Traits::compare(query, split_point_);
  if (cmp < 0) {
    // None of the intervals in right_ may intersect this.
    if (left_ != NULL) {
      left_->FindContainingPoint(query, results);
    }

    // Any intervals which start before the query point and overlap the split point
    // must therefore contain the query point.
    for (const interval_type &interval : overlapping_by_asc_left_) {
      if (Traits::compare(Traits::get_left(interval), query) <= 0) {
        results->push_back(interval);
      } else {
        break;
      }
    }
  } else if (cmp > 0) {
    // None of the intervals in left_ may intersect this.
    if (right_ != NULL) {
      right_->FindContainingPoint(query, results);
    }

    // Any intervals which end after the query point and overlap the split point
    // must therefore contain the query point.
    for (const interval_type &interval : overlapping_by_desc_right_) {
      if (Traits::compare(Traits::get_right(interval), query) >= 0) {
        results->push_back(interval);
      } else {
        break;
      }
    }
  } else {
    DCHECK_EQ(cmp, 0);
    // The query is exactly our split point -- in this case we've already got
    // the computed list of overlapping intervals.
    results->insert(results->end(), overlapping_by_asc_left_.begin(),
                    overlapping_by_asc_left_.end());
  }
}

template<class Traits>
void ITNode<Traits>::FindIntersectingInterval(const interval_type &query,
                                              IntervalVector *results) const {
  if (Traits::compare(Traits::get_right(query), split_point_) < 0) {
    // The interval is fully left of the split point. So, it may not overlap
    // with any in 'right_'
    if (left_ != NULL) {
      left_->FindIntersectingInterval(query, results);
    }

    // Any intervals whose left edge is <= the query interval's right edge
    // intersect the query interval.
    for (const interval_type &interval : overlapping_by_asc_left_) {
      if (Traits::compare(Traits::get_left(interval),Traits::get_right(query)) <= 0) {
        results->push_back(interval);
      } else {
        break;
      }
    }
  } else if (Traits::compare(Traits::get_left(query), split_point_) > 0) {
    // The interval is fully right of the split point. So, it may not overlap
    // with any in 'left_'
    if (right_ != NULL) {
      right_->FindIntersectingInterval(query, results);
    }

    // Any intervals whose right edge is >= the query interval's left edge
    // intersect the query interval.
    for (const interval_type &interval : overlapping_by_desc_right_) {
      if (Traits::compare(Traits::get_right(interval), Traits::get_left(query)) >= 0) {
        results->push_back(interval);
      } else {
        break;
      }
    }
  } else {
    // The query interval contains the split point. Therefore all other intervals
    // which also contain the split point are intersecting.
    results->insert(results->end(), overlapping_by_asc_left_.begin(),
                    overlapping_by_asc_left_.end());

    // The query interval may _also_ intersect some in either child.
    if (left_ != NULL) {
      left_->FindIntersectingInterval(query, results);
    }
    if (right_ != NULL) {
      right_->FindIntersectingInterval(query, results);
    }
  }
}


} // namespace interval_tree_internal

} // namespace kudu

#endif
