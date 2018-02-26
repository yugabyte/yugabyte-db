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

#ifndef YB_DOCDB_JSONB_H
#define YB_DOCDB_JSONB_H

#include <rapidjson/document.h>

#include "yb/util/status.h"

namespace yb {
namespace docdb {

using JsonbMetadata = uint32_t;
using JsonbHeader = JsonbMetadata;
using JEntry = JsonbMetadata;

// Jsonb is a serialization format for json that used in postgresql. This implementation of jsonb
// is similar to the jsonb format, although not exactly the same (details regarding differences
// follow). The jsonb format, first includes a 32 bit header, whose first 28 bits store the total
// number of key-value pairs in the json object. The next four bits are used to indicate whether
// this is a json object, json array or just a scalar value.
//
// Next, we store the metadata for all the keys and values in the json object. The key-value
// pairs are sorted based on keys before serialization and hence the original order is lost.
// However, the sorting of key-value pairs would make it easier to search for a particular key in
// jsonb. After the 32 bit jsonb header, we store 32 bit metadata for each key, followed by a
// 32 bit metadata for each value. Next, we store all the keys followed by all the values.
//
// In case of arrays, we store the metadata for all the array elements first and then store the
// data for the corresponding array elements after that. The original order of the array elements
// is maintained.
//
// The 32 bit metadata is called a JEntry and the first 28 bits store the ending offset of the
// data. The last 4 bits indicate the type of the data (ex: string, numeric, bool, array, object
// or null).
//
// The following are some of the differences from postgresql's jsonb implementation:
// 1. In the JEntry, postgresql sometimes stores offsets and sometimes stores the length. This is
// done for better compressibility in their case. Although, for us this doesn't make much of a
// difference and hence its simpler to just use offsets.
// 2. In our serialization format, we just use the BigEndian format used in docdb to store
// serialized integers.
// 3. We store the data type for ints, uints, floats and doubles in the JEntry.
// 4. We store information about whether a container is an array or an object in the JEntry.
class Jsonb {
 public:
  // Creates a serialized jsonb string from plaintext json.
  static CHECKED_STATUS ToJsonb(const std::string& json, std::string* jsonb);
  // Builds a json document from serialized jsonb.
  static CHECKED_STATUS FromJsonb(const std::string& jsonb, rapidjson::Document* document);
 private:
  static CHECKED_STATUS ToJsonbInternal(const rapidjson::Value& document, std::string* jsonb);
  static CHECKED_STATUS ToJsonbProcessObject(const rapidjson::Value& document,
                                             std::string* jsonb);
  static CHECKED_STATUS ToJsonbProcessArray(const rapidjson::Value& document,
                                            std::string* jsonb);
  static CHECKED_STATUS ProcessJsonValue(const rapidjson::Value& value,
                                         const size_t data_begin_offset,
                                         std::string* jsonb,
                                         size_t* metadata_offset);

  // Method to recursively build the json object from serialized jsonb. The offset denotes the
  // starting position in the jsonb from which we need to start processing.
  static CHECKED_STATUS FromJsonbInternal(const std::string& jsonb, size_t offset,
                                          rapidjson::Document* document);
  static CHECKED_STATUS FromJsonbProcessObject(const std::string& jsonb, size_t offset,
                                               const JsonbHeader& jsonb_header,
                                               rapidjson::Document* document);
  static CHECKED_STATUS FromJsonbProcessArray(const std::string& jsonb, size_t offset,
                                              const JsonbHeader& jsonb_header,
                                              rapidjson::Document* document);

  static pair<size_t, size_t> ComputeOffsetsAndJsonbHeader(size_t num_entries,
                                                           uint32_t container_type,
                                                           std::string* jsonb);

  // Helper method to retrieve the (offset, length) of a key/value serialized in jsonb format.
  // element_metadata_offset denotes the offset for the JEntry of the key/value,
  // element_end_offset denotes the end of data portion of the key/value, data_begin_offset
  // denotes the offset from which the data portion of jsonb starts, metadata_begin_offset is the
  // offset from which all the JEntry fields begin.
  static pair<size_t, size_t> GetOffsetAndLength(size_t element_metadata_offset,
                                                 const std::string& jsonb,
                                                 size_t element_end_offset,
                                                 size_t data_begin_offset,
                                                 size_t metadata_begin_offset);

  // Bit masks for jsonb header fields.
  static constexpr uint32_t kJBCountMask = 0x0FFFFFFF; // mask for number of kv pairs.
  static constexpr uint32_t kJBScalar = 0x10000000; // indicates whether we have a scalar value.
  static constexpr uint32_t kJBObject = 0x20000000; // indicates whether we have a json object.
  static constexpr uint32_t kJBArray = 0x40000000; // indicates whether we have a json array.

  // Bit masks for json header fields.
  static constexpr uint32_t kJEOffsetMask = 0x0FFFFFFF;
  static constexpr uint32_t kJETypeMask = 0xF0000000;

  // Values stored in the type bits.
  static constexpr uint32_t kJEIsString = 0x00000000;
  static constexpr uint32_t kJEIsObject = 0x10000000;
  static constexpr uint32_t kJEIsBoolFalse = 0x20000000;
  static constexpr uint32_t kJEIsBoolTrue = 0x30000000;
  static constexpr uint32_t kJEIsNull = 0x40000000;
  static constexpr uint32_t kJEIsArray = 0x50000000;
  static constexpr uint32_t kJEIsInt = 0x60000000;
  static constexpr uint32_t kJEIsUInt = 0x70000000;
  static constexpr uint32_t kJEIsInt64 = 0x80000000;
  static constexpr uint32_t kJEIsUInt64 = 0x90000000;
  static constexpr uint32_t kJEIsFloat = 0xA0000000;
  static constexpr uint32_t kJEIsDouble = 0xB0000000;
};

} // namespace docdb
} // namespace yb

#endif // YB_DOCDB_JSONB_H
