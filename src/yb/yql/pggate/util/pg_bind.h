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

#ifndef YB_YQL_PGGATE_UTIL_PG_BIND_H_
#define YB_YQL_PGGATE_UTIL_PG_BIND_H_

#include <algorithm>
#include <memory>
#include "yb/yql/pggate/util/pg_net_reader.h"

namespace yb {
namespace pggate {

class PgBind {
 public:
  // Public types.
  typedef std::shared_ptr<PgBind> SharedPtr;
  typedef std::shared_ptr<const PgBind> SharedPtrConst;

  typedef std::unique_ptr<PgBind> UniPtr;
  typedef std::unique_ptr<const PgBind> UniPtrConst;

  // Constructor.
  PgBind() { }
  virtual ~PgBind() { }

  // Write the value from YugaByte storage to Posgres buffer.
  virtual size_t Read(PgNetBuffer *buffer) = 0;
};

//--------------------------------------------------------------------------------------------------
// Numeric Types.
//--------------------------------------------------------------------------------------------------
template<typename num_type>
class PgBindNumber : public PgBind {
 public:
  explicit PgBindNumber(num_type *holder) : holder_(holder) {
  }
  virtual size_t Read(PgNetBuffer *buffer) override {
    size_t read_size = PgNetReader::ReadNumber(buffer, holder_);
    buffer->advance_cursor(read_size);
    return read_size;
  }
 private:
  num_type *holder_;
};

typedef PgBindNumber<uint8_t> PgBindUint8;
typedef PgBindNumber<int16_t> PgBindInt16;
typedef PgBindNumber<int32_t> PgBindInt32;
typedef PgBindNumber<int64_t> PgBindInt64;

typedef PgBindNumber<float> PgBindFloat;
typedef PgBindNumber<double> PgBindDouble;

//--------------------------------------------------------------------------------------------------
// String or Text Types.
//--------------------------------------------------------------------------------------------------
// SQL fixed sizes for string.
// - CHAR
class PgBindChar : public PgBind {
 public:
  PgBindChar(char *holder, int64_t *bytes, bool allow_truncate)
    : holder_(holder), capacity_(*bytes), bytes_(bytes), allow_truncate_(allow_truncate) {
    CHECK(allow_truncate_) << "Need working on truncation";
  }
  virtual size_t Read(PgNetBuffer *buffer) override;
 private:
  char *holder_;
  const int64_t capacity_;
  int64_t *bytes_;

  // TODO(neil) We assume truncation is allowed for now. Need to correct this behavior when things
  // start working.
  bool allow_truncate_;
};

// SQL variable sizes for string.
// - VARCHAR
// - TEXT
class PgBindText : public PgBind {
 public:
  PgBindText(char *holder, int64_t *bytes, bool allow_truncate)
    : holder_(holder), capacity_(*bytes), bytes_(bytes), allow_truncate_(allow_truncate) {
    CHECK(allow_truncate_) << "Need working on truncation";
  }
  virtual size_t Read(PgNetBuffer *buffer) override;
 private:
  char *holder_;
  const int64_t capacity_;
  int64_t *bytes_;

  // TODO(neil) We assume truncation is allowed for now. Need to correct this behavior when things
  // start working.
  bool allow_truncate_;
};

//--------------------------------------------------------------------------------------------------
// Binary string types.
//--------------------------------------------------------------------------------------------------

}  // namespace pggate
}  // namespace yb

#endif // YB_YQL_PGGATE_UTIL_PG_BIND_H_
