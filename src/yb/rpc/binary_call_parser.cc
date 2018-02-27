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

#include "yb/rpc/binary_call_parser.h"

#include "yb/gutil/endian.h"

namespace yb {
namespace rpc {

BinaryCallParser::BinaryCallParser(
    size_t header_size, size_t size_offset, size_t max_message_length, IncludeHeader include_header,
    BinaryCallParserListener* listener)
    : buffer_(header_size), size_offset_(size_offset), max_message_length_(max_message_length),
      include_header_(include_header), listener_(listener) {
}

Result<size_t> BinaryCallParser::Parse(const rpc::ConnectionPtr& connection, const IoVecs& data) {
  auto full_size = IoVecsFullSize(data);

  size_t consumed = 0;
  const size_t header_size = buffer_.size();
  while (full_size >= consumed + header_size) {
    IoVecsToBuffer(data, consumed, consumed + header_size, &buffer_);

    size_t data_length = NetworkByteOrder::Load32(buffer_.data() + size_offset_);
    const size_t total_length = data_length + header_size;
    if (total_length > max_message_length_) {
      return STATUS_FORMAT(
          NetworkError,
          "The frame had a length of $0, but we only support messages up to $1 bytes long.",
          total_length, max_message_length_);
    }
    if (consumed + total_length > full_size) {
      break;
    }

    std::vector<char> call_data;
    IoVecsToBuffer(
        data, consumed + (include_header_ ? 0 : header_size), consumed + total_length, &call_data);
    RETURN_NOT_OK(listener_->HandleCall(connection, &call_data));

    consumed += total_length;
  }

  return consumed;
}

} // namespace rpc
} // namespace yb
