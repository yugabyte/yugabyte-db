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

#include "yb/util/flags/flag_tags.h"

#include <map>
#include <string>
#include <unordered_set>
#include <utility>

#include "yb/gutil/map-util.h"
#include "yb/gutil/singleton.h"

using std::multimap;
using std::pair;
using std::string;
using std::unordered_set;

DEFINE_test_flag(bool, running_test, false, "Flag that is set to true when we running a test");

namespace yb {
namespace flag_tags_internal {

// Singleton registry storing the set of tags for each flag.
class FlagTagRegistry {
 public:
  static FlagTagRegistry* GetInstance() {
    return Singleton<FlagTagRegistry>::get();
  }

  void Tag(const string& name, const FlagTag& tag) {
    tag_map_.insert(TagMap::value_type(name, tag));
  }

  void GetTags(const string& name, unordered_set<FlagTag>* tags) {
    tags->clear();
    pair<TagMap::const_iterator, TagMap::const_iterator> range =
      tag_map_.equal_range(name);
    for (auto it = range.first; it != range.second; ++it) {
      if (!InsertIfNotPresent(tags, it->second)) {
        LOG(DFATAL) << "Flag " << name << " was tagged more than once with the tag '"
                    << ToString(it->second) << "'";
      }
    }
  }

 private:
  friend class Singleton<FlagTagRegistry>;
  FlagTagRegistry() {}

  typedef multimap<string, FlagTag> TagMap;
  TagMap tag_map_;

  DISALLOW_COPY_AND_ASSIGN(FlagTagRegistry);
};

FlagTagger::FlagTagger(const char* name, const FlagTag& tag) {
  FlagTagRegistry::GetInstance()->Tag(name, tag);
}

FlagTagger::~FlagTagger() {
}

} // namespace flag_tags_internal

using flag_tags_internal::FlagTagRegistry;

void GetFlagTags(const string& flag_name, unordered_set<FlagTag>* tags) {
  FlagTagRegistry::GetInstance()->GetTags(flag_name, tags);
}

} // namespace yb
