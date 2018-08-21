//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_PGGATE_UTIL_PG_TUPLE_H_
#define YB_YQL_PGGATE_UTIL_PG_TUPLE_H_

#include "yb/yql/pggate/util/pg_wire.h"

namespace yb {
namespace pggate {

// PgTuple.
// TODO(neil) This code needs to be optimize. We might be able to use DocDB buffer directly for
// most datatype except numeric. A simpler optimization would be allocate one buffer for each
// tuple and write the value there.
//
// Currently we allocate one individual buffer per column and write result there.
class PgTuple {
 public:
  PgTuple(uint64_t *datums, bool *isnulls);

  // Write null value.
  void WriteNull(int index, const PgWireDataHeader& header);

  // Write data in Postgres format.
  void Write(int index, const PgWireDataHeader& header, bool value);
  void Write(int index, const PgWireDataHeader& header, int16_t value);
  void Write(int index, const PgWireDataHeader& header, int32_t value);
  void Write(int index, const PgWireDataHeader& header, int64_t value);
  void Write(int index, const PgWireDataHeader& header, float value);
  void Write(int index, const PgWireDataHeader& header, double value);
  void Write(int index, const PgWireDataHeader& header, const char *value, int64_t bytes);
  void Write(int index, const PgWireDataHeader& header, const uint8_t *value, int64_t bytes);

 private:
  uint64_t *datums_;
  bool *isnulls_;


  // TODO(neil) The following code supports allocating one buffer and write result to the buffer
  // with correct alignment for each type of data.
#if 0
  uint8_t *buffer_;
  int64_t buffer_bytes_;
  int64_t offset_;

  template<uint64_t alignof_type>
  static uint64_t AlignedSize(uint64_t bytes) {
    return (bytes + alignof_type - 1) & ~(alignof_type - 1);
  }

  template<typename data_type>
  static uint64_t TypeAlignedSize(uint64_t bytes) {
    return AlignedSize<alignof(data_type)>(bytes);
  }

  template<uint64_t alignof_type>
  uint8_t *AlignedBuffer() {
    offset_ = (offset_ + alignof_type - 1) & ~(alignof_type - 1);
    return buffer_ + offset_;
  }

  template<typename data_type>
  uint8_t *TypeAlignedBuffer() {
    return AlignedBuffer<alignof(data_type)>();
  }
#endif
};

}  // namespace pggate
}  // namespace yb

#endif // YB_YQL_PGGATE_UTIL_PG_TUPLE_H_
