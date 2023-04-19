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

#include <stdint.h>
#include <vector>

namespace yb {

// This is a class to hold ordered sets of positive integers. This
// class provides methods to create groups and retrieve a group a particular
// integer is a member of in constant time.
class ColGroupHolder {
 public:

  explicit ColGroupHolder(size_t num_cols) {
    col_to_group_.resize(num_cols, -1);
    groups_.clear();
  }

  // Starts a new group in this holder. Any subsequent calls to AddToLatestGroup
  // will add to this new group.
  void BeginNewGroup() {
    DCHECK(groups_.size() == 0 ||
           groups_.back().size() > 0);
    groups_.push_back({});
  }

  // Adds idx to the latest group that was created as a result of BeginNewGroup.
  void AddToLatestGroup(size_t idx) {
    DCHECK(std::find(groups_.back().begin(), groups_.back().end(), idx) ==
           groups_.back().end());
    DCHECK_LT(col_to_group_[idx], 0);

    col_to_group_[idx] = static_cast<int>(groups_.size() - 1);
    groups_.back().push_back(idx);
  }

  // Returns a reference to the group idx is a part of. idx must have been
  // added to a group in this ColGroupHolder.
  const std::vector<size_t> &GetGroup(size_t idx) const {
    DCHECK_LT(idx, col_to_group_.size());
    DCHECK_GE(col_to_group_[idx], 0);
    DCHECK_LT(col_to_group_[idx], groups_.size());
    return groups_[col_to_group_[idx]];
  }

 private:
  std::vector<std::vector<size_t>> groups_;

  // If col_to_group_[i] < 0 then i is not in any group.
  std::vector<int> col_to_group_;
};

};  // namespace yb
