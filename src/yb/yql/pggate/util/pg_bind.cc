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
//
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pggate/util/pg_bind.h"
#include "yb/yql/pggate/util/pg_net_reader.h"

namespace yb {
namespace pggate {

//--------------------------------------------------------------------------------------------------
size_t PgBindChar::Read(PgNetBuffer *buffer) {
  // TODO(neil) HACK for now. We need to provide a mechanism for users to read chunk by chunk to
  // a fixed size buffer.
  *bytes_ = 0;
  CHECK(allow_truncate_) << "Need working on truncation";

  int64_t data_size;
  size_t read_size = PgNetReader::ReadNumber(buffer, &data_size);
  CHECK_LE(data_size, capacity_);
  *bytes_ = data_size;
  buffer->advance_cursor(read_size);

  read_size = PgNetReader::ReadBytes(buffer, holder_, data_size);
  holder_[data_size] = 0;
  buffer->advance_cursor(read_size);

  return read_size;
}

size_t PgBindText::Read(PgNetBuffer *buffer) {
  // TODO(neil) HACK for now. We need to provide a mechanism for users to read chunk by chunk to
  // a fixed size buffer.
  *bytes_ = 0;
  CHECK(allow_truncate_) << "Need working on truncation";

  int64_t data_size;
  size_t read_size = PgNetReader::ReadNumber(buffer, &data_size);
  CHECK_LE(data_size, capacity_);
  *bytes_ = data_size;
  buffer->advance_cursor(read_size);

  read_size = PgNetReader::ReadBytes(buffer, holder_, data_size);
  holder_[data_size] = 0;
  buffer->advance_cursor(read_size);

  return read_size;
}

}  // namespace pggate
}  // namespace yb
