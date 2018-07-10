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

#include "yb/yql/pggate/util/pg_net_reader.h"
#include "yb/yql/pggate/util/pg_bind.h"
#include "yb/client/client.h"

namespace yb {
namespace pggate {

CHECKED_STATUS PgNetReader::ReadNewTuples(const string& net_data,
                                          int64_t *total_row_count,
                                          PgNetBuffer *buffer) {
  // Setup the buffer to read the next set of tuples.
  CHECK(buffer->empty()) << "Existing buffer is not yet fully read";
  buffer->set_cursor(net_data);

  // Read the number row_count in this set.
  int64_t this_count;
  size_t read_size = ReadNumber(buffer, &this_count);
  *total_row_count += this_count;
  buffer->advance_cursor(read_size);

  return Status::OK();
}

CHECKED_STATUS PgNetReader::ReadTuple(const vector<PgBind::SharedPtr>& pg_binds,
                                      PgNetBuffer *buffer) {

  for (const PgBind::SharedPtr &pg_bind : pg_binds) {
    size_t read_size;
    uint8_t header_data;

    // Read for NULL value.
    read_size = ReadNumber(buffer, &header_data);
    buffer->advance_cursor(read_size);
    PgColumnHeader header(header_data);

    if (!header.is_null()) {
      // Read the NOT-NULL data.
      pg_bind->Read(buffer);
    }
  }
  return Status::OK();
}

size_t PgNetReader::ReadNumber(PgNetBuffer *buffer, uint8 *value) {
  *value = *reinterpret_cast<const uint8*>(buffer->data());
  return sizeof(uint8_t);
}

size_t PgNetReader::ReadNumber(PgNetBuffer *buffer, int16 *value) {
  return ReadNumericValue(NetworkByteOrder::Load16, buffer, reinterpret_cast<uint16*>(value));
}

size_t PgNetReader::ReadNumber(PgNetBuffer *buffer, int32 *value) {
  return ReadNumericValue(NetworkByteOrder::Load32, buffer, reinterpret_cast<uint32*>(value));
}

size_t PgNetReader::ReadNumber(PgNetBuffer *buffer, int64 *value) {
  return ReadNumericValue(NetworkByteOrder::Load64, buffer, reinterpret_cast<uint64*>(value));
}

size_t PgNetReader::ReadNumber(PgNetBuffer *buffer, float *value) {
  uint32 int_value;
  size_t read_size = ReadNumericValue(NetworkByteOrder::Load32, buffer, &int_value);
  *value = *reinterpret_cast<float*>(&int_value);
  return read_size;
}

size_t PgNetReader::ReadNumber(PgNetBuffer *buffer, double *value) {
  uint64 int_value;
  size_t read_size = ReadNumericValue(NetworkByteOrder::Load64, buffer, &int_value);
  *value = *reinterpret_cast<double*>(&int_value);
  return read_size;
}

// Read Text Data -------------------------------------------------------------------------------
size_t PgNetReader::ReadBytes(PgNetBuffer *buffer, char *value, int64_t bytes) {
  memcpy(value, buffer->data(), bytes);
  return bytes;
}

}  // namespace pggate
}  // namespace yb
