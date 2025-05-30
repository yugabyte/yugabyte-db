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

#include <boost/intrusive/slist.hpp>
#include <boost/intrusive/trivial_value_traits.hpp>

#include "yb/util/cast.h"
#include "yb/util/crash_point.h"
#include "yb/util/shmem/annotations.h"

namespace yb {

struct RobustIntrusiveListNode {
  ChildProcessRW<RobustIntrusiveListNode*> next;
};

struct RobustIntrusiveListNodeTraits {
  using node = RobustIntrusiveListNode;
  using node_ptr = node*;
  using const_node_ptr = const node*;

  static node_ptr get_next(const_node_ptr p) {
    return SHARED_MEMORY_LOAD(p->next);
  }

  static void set_next(node_ptr n, node_ptr next) {
    SHARED_MEMORY_STORE(n->next, next);
    TEST_CRASH_POINT("RobustIntrusiveList::set_next:1");
  }
};

template<typename Node>
struct RobustIntrusiveListValueTraits {
  using node_traits = RobustIntrusiveListNodeTraits;
  using value_type = Node;
  using node_ptr = node_traits::node_ptr;
  using const_node_ptr = node_traits::const_node_ptr;
  using pointer = value_type*;
  using const_pointer = const value_type*;

  static constexpr boost::intrusive::link_mode_type link_mode =
      boost::intrusive::normal_link;

  static constexpr node_ptr to_node_ptr(value_type& value) {
    return pointer_cast<node_ptr>(&value);
  }
  static constexpr const_node_ptr to_node_ptr(const value_type& value) {
    return pointer_cast<const_node_ptr>(&value);
  }
  static constexpr pointer to_value_ptr(node_ptr n) {
    return pointer_cast<pointer>(n);
  }
  static constexpr const_pointer to_value_ptr(const_node_ptr n) {
    return pointer_cast<const_pointer>(n);
  }
};

template<typename Node>
using RobustIntrusiveList =
    boost::intrusive::slist<
        Node,
        boost::intrusive::value_traits<RobustIntrusiveListValueTraits<Node>>,
        // Constant time size has a size field, which is not reliable, as it may not be updated
        // properly if we crash.
        boost::intrusive::constant_time_size<false>,
        boost::intrusive::linear<true>,
        boost::intrusive::cache_last<false>>;

} // namespace yb
