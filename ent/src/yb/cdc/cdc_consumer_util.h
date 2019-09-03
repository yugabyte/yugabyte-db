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

#include <stdlib.h>
#include <string>
#include <boost/functional/hash.hpp>

#ifndef ENT_SRC_YB_CDC_CDC_CONSUMER_UTIL_H
#define ENT_SRC_YB_CDC_CDC_CONSUMER_UTIL_H

namespace yb {
namespace cdc {

struct ConsumerTabletInfo {
  std::string tablet_id;
  std::string table_id;
};

struct ProducerTabletInfo {
  // TODO(Rahul): Add universe_uuid when we support 3DC.
  std::string stream_id;
  std::string tablet_id;

  bool operator==(const ProducerTabletInfo& other) const {
    return stream_id == other.stream_id && tablet_id == other.tablet_id;
  }

  struct Hash {
    std::size_t operator()(const ProducerTabletInfo& p) const noexcept {
      std::size_t hash = 0;
      boost::hash_combine(hash, p.stream_id);
      boost::hash_combine(hash, p.tablet_id);

      return hash;
    }
  };
};

} // namespace cdc
} // namespace yb


#endif // ENT_SRC_YB_CDC_CDC_CONSUMER_UTIL_H
