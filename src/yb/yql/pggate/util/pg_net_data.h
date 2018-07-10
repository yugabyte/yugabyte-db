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

#ifndef YB_YQL_PGGATE_UTIL_PG_NET_DATA_H_
#define YB_YQL_PGGATE_UTIL_PG_NET_DATA_H_

#include <bitset>

namespace yb {
namespace pggate {

// Just in case we change the serialization format. Different versions of DocDB and Postgres
// support can work with multiple data formats.
// - Rolling upgrade will need this.
// - TODO(neil) yb_client should have information on what pg_format_version should be used.
// GFLAG for CurrentDataFormatVersion;

// Result Set Header -----------------------------------------------------------------------------
// We'll use one byte to represent column metadata.
//   Bit 0x01 - NULL indicator
//   Bit 0x02 - unused
//   Bit 0x04 - unused
//   Bit 0x08 - unused
//   ....
class PgColumnHeader {
 public:
  PgColumnHeader() {
  }

  explicit PgColumnHeader(uint8_t val) : data_(val) {
  }

  void set_null() {
    data_[0] = 1;
  }
  bool is_null() {
    return data_[0] == 1;
  }

  uint8_t ToUint8() {
    return static_cast<uint8_t>(data_.to_ulong());
  }

 private:
  std::bitset<8> data_;
};

class PgNetBuffer {
 public:
  explicit PgNetBuffer(char *data = nullptr, size_t bytes = 0) : data_(data), bytes_(bytes) {
  }

  const char *data() const {
    return data_;
  }
  size_t bytes() const {
    return bytes_;
  }

  bool empty() const {
    return data_ == nullptr && bytes_ == 0;
  }

  void set_cursor(const string& net_data) {
    data_ = net_data.data();
    bytes_ = net_data.size();
  }

  void advance_cursor(size_t read_size) {
    bytes_ -= read_size;
    data_ = bytes_ == 0 ? nullptr : data_ + read_size;
  }

 private:
  const char *data_;
  size_t bytes_;
};

}  // namespace pggate
}  // namespace yb

#endif // YB_YQL_PGGATE_UTIL_PG_NET_DATA_H_
