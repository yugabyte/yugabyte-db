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

#include "yb/cdc/cdc_util.h"

#include <boost/functional/hash.hpp>

#include "yb/gutil/strings/stringpiece.h"
#include "yb/util/format.h"

namespace yb::cdc {

std::string TabletStreamInfo::ToString() const {
  return Format("{ stream_id: $1 tablet_id: $2 }", stream_id, tablet_id);
}

std::size_t TabletStreamInfo::Hash::operator()(const TabletStreamInfo& p) const noexcept {
  std::size_t hash = 0;
  boost::hash_combine(hash, p.stream_id);
  boost::hash_combine(hash, p.tablet_id);

  return hash;
}

}  // namespace yb::cdc
