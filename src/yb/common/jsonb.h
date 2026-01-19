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

#include <string>

#include <rapidjson/document.h>

#include "yb/common/common_fwd.h"

#include "yb/util/slice.h"
#include "yb/util/status_fwd.h"

namespace yb {
namespace common {

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
  Jsonb();

  // Creates an object from a serialized jsonb payload.
  explicit Jsonb(const std::string& jsonb);
  explicit Jsonb(std::string_view jsonb);

  explicit Jsonb(std::string&& jsonb);

  void Assign(std::string&& jsonb);

  // Creates a serialized jsonb string from plaintext json.
  Status FromString(const std::string& json);

  // Creates a serialized jsonb string from rapidjson document or value.
  Status FromRapidJson(const rapidjson::Document& document);
  Status FromRapidJson(const rapidjson::Value& value);

  // Creates a serialized jsonb string from QLValuePB.
  Status FromQLValue(const LWQLValuePB& value_pb);
  Status FromQLValue(const QLValuePB& value_pb);
  Status FromQLValue(const QLValue& value);

  // Builds a json document from serialized jsonb.
  Status ToRapidJson(rapidjson::Document* document) const;

  // Returns a json string for serialized jsonb
  Status ToJsonString(std::string* json) const;

  static Status ApplyJsonbOperators(std::string_view serialized_json,
                                    const QLJsonColumnOperationsPB& json_ops,
                                    QLValuePB* result);

  static Status ApplyJsonbOperators(std::string_view serialized_json,
                                    const LWQLJsonColumnOperationsPB& json_ops,
                                    LWQLValuePB* result);

  const std::string& SerializedJsonb() const;

  // Use with extreme care since this destroys the internal state of the object. The only purpose
  // for this method is to allow for efficiently moving the serialized jsonb.
  std::string&& MoveSerializedJsonb();

  bool operator==(const Jsonb& other) const;

  static std::string kSerializedJsonbEmpty;
  static std::string kSerializedJsonbNull;

 private:
  std::string serialized_jsonb_;

  // Given a jsonb slice, it applies the given operator to the slice and returns the result as a
  // Slice and the element's metadata.
  template<class Op>
  static Status ApplyJsonbOperator(
      Slice jsonb, const Op& json_op, Slice* result, JEntry* element_metadata);

  static Status ToJsonStringInternal(const Slice& jsonb, std::string* json);
  static Status ToJsonbInternal(const rapidjson::Value& document, std::string* jsonb);
  static Status ToJsonbProcessObject(const rapidjson::Value& document,
                                             std::string* jsonb);
  static Status ToJsonbProcessArray(const rapidjson::Value& document,
                                            bool is_scalar,
                                            std::string* jsonb);
  static Status ProcessJsonValueAndMetadata(const rapidjson::Value& value,
                                                    const size_t data_begin_offset,
                                                    std::string* jsonb,
                                                    size_t* metadata_offset);

  // Method to recursively build the json object from serialized jsonb. The offset denotes the
  // starting position in the jsonb from which we need to start processing.
  static Status FromJsonbInternal(const Slice& jsonb, rapidjson::Document* document);
  static Status FromJsonbProcessObject(const Slice& jsonb,
                                               const JsonbHeader& jsonb_header,
                                               rapidjson::Document* document);
  static Status FromJsonbProcessArray(const Slice& jsonb,
                                              const JsonbHeader& jsonb_header,
                                              rapidjson::Document* document);

  static std::pair<size_t, size_t> ComputeOffsetsAndJsonbHeader(size_t num_entries,
                                                                uint32_t container_type,
                                                                std::string* jsonb);

  template <class Ops, class Value>
  static Status DoApplyJsonbOperators(
      std::string_view serialized_json, const Ops& json_ops, Value* result);
};

} // namespace common
} // namespace yb
