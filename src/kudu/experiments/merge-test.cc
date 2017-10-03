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

#include <boost/lexical_cast.hpp>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <algorithm>
#include <string>
#include <vector>

#include "kudu/util/stopwatch.h"

DEFINE_int32(num_lists, 3, "Number of lists to merge");
DEFINE_int32(num_rows, 100, "Number of entries per list");
DEFINE_int32(num_iters, 5, "Number of times to run merge");

using std::vector;
using std::string;

typedef string MergeType;

struct CompareIters {
  explicit CompareIters(vector<vector<MergeType>::const_iterator> *iters) :
    iters_(iters)
  {}

  bool operator()(int left, int right) {
    return *((*iters_)[left]) >= *((*iters_)[right]);
  }

  vector<vector<MergeType>::const_iterator> *iters_;
};

void HeapMerge(
  const vector<vector<MergeType> > &in_lists,
  vector<MergeType> *out) {
  typedef vector<MergeType>::const_iterator MergeTypeIter;

  vector<MergeTypeIter> iters;
  vector<size_t> indexes;
  size_t i = 0;
  for (const vector<MergeType> &list : in_lists) {
    iters.push_back(list.begin());
    indexes.push_back(i++);
  }

  CompareIters comp(&iters);
  std::make_heap(indexes.begin(), indexes.end(), comp);

  while (!indexes.empty()) {
    size_t min_idx = indexes.front();
    MergeTypeIter &min_iter = iters[min_idx];

    out->push_back(*min_iter);

    min_iter++;
    std::pop_heap(indexes.begin(), indexes.end(), comp);
    if (min_iter == in_lists[min_idx].end()) {
      indexes.pop_back();
    } else {
      std::push_heap(indexes.begin(), indexes.end(), comp);
    }
  }
}

void SimpleMerge(const vector<vector<MergeType> > &in_lists,
                 vector<MergeType> *out) {
  typedef vector<MergeType>::const_iterator MergeTypeIter;
  vector<MergeTypeIter> iters;
  for (const vector<MergeType> &list : in_lists) {
    iters.push_back(list.begin());
  }

  while (true) {
    MergeTypeIter *smallest = nullptr;
    for (int i = 0; i < in_lists.size(); i++) {
      if (iters[i] == in_lists[i].end()) continue;
      if (smallest == nullptr ||
          *iters[i] < **smallest) {
        smallest = &iters[i];
      }
    }

    if (smallest == nullptr) break;

    out->push_back(**smallest);
    (*smallest)++;
  }
}

int main(int argc, char **argv) {
  google::ParseCommandLineFlags(&argc, &argv, true);

  vector<vector<MergeType> > in_lists;
  in_lists.resize(FLAGS_num_lists);

  for (int i = 0; i < FLAGS_num_lists; i++) {
    vector<MergeType> &list = in_lists[i];

    int entry = 0;
    for (int j = 0; j < FLAGS_num_rows; j++) {
      entry += rand() % 5;
      list.push_back(boost::lexical_cast<MergeType>(entry));
    }
  }

  for (int i = 0; i < FLAGS_num_iters; i++) {
    vector<MergeType> out;
    out.reserve(FLAGS_num_lists * FLAGS_num_rows);

    LOG_TIMING(INFO, "HeapMerge") {
      HeapMerge(in_lists, &out);
    }
  }

  for (int i = 0; i < FLAGS_num_iters; i++) {
    vector<MergeType> out;
    out.reserve(FLAGS_num_lists * FLAGS_num_rows);

    LOG_TIMING(INFO, "SimpleMerge") {
      SimpleMerge(in_lists, &out);
    }
  }

  return 0;
}
