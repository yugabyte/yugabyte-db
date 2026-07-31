// Copyright (c) YugabyteDB, Inc.
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

#include <boost/container/small_vector.hpp>
#include <stddef.h>

#include "yb/util/status.h"
#include "yb/dockv/pg_row.h"
#include "yb/util/slice.h"

namespace yb {
class ColumnSchema;
class Schema;
namespace dockv {
struct ReaderProjection;
}  // namespace dockv
}  // namespace yb

namespace yb::dockv {

using PgKeyColumnDecoder = UnsafeStatus(*)(
    const char*, const char*, PgTableRow*, size_t, void*const*);

class PgKeyDecoder {
 public:
  PgKeyDecoder(const Schema& schema, const ReaderProjection& projection);
  ~PgKeyDecoder();

  Status Decode(Slice key, PgTableRow* out) const;

  static Status DecodeEntry(
      Slice* key, const ColumnSchema& column, PgTableRow* out, size_t index);

 private:
  boost::container::small_vector<PgKeyColumnDecoder, 0x10> decoders_;
};

}  // namespace yb::dockv
