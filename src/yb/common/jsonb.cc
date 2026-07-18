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
#include "yb/common/jsonb.h"

#include <rapidjson/error/en.h>

#include "yb/common/common.messages.h"
#include "yb/common/json_util.h"
#include "yb/common/ql_value.h"

#include "yb/gutil/casts.h"

#include "yb/util/kv_util.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/varint.h"

using std::string;

namespace yb {
namespace common {

namespace {

// Values stored in the type bits.
constexpr uint32_t kJEIsString = 0x00000000;
constexpr uint32_t kJEIsObject = 0x10000000;
constexpr uint32_t kJEIsBoolFalse = 0x20000000;
constexpr uint32_t kJEIsBoolTrue = 0x30000000;
constexpr uint32_t kJEIsNull = 0x40000000;
constexpr uint32_t kJEIsArray = 0x50000000;
constexpr uint32_t kJEIsInt = 0x60000000;
constexpr uint32_t kJEIsUInt = 0x70000000;
constexpr uint32_t kJEIsInt64 = 0x80000000;
constexpr uint32_t kJEIsUInt64 = 0x90000000;
constexpr uint32_t kJEIsFloat = 0xA0000000;
constexpr uint32_t kJEIsDouble = 0xB0000000;

// Bit masks for jsonb header fields.
constexpr uint32_t kJBCountMask = 0x0FFFFFFF; // mask for number of kv pairs.
constexpr uint32_t kJBScalar = 0x10000000; // indicates whether we have a scalar value.
constexpr uint32_t kJBObject = 0x20000000; // indicates whether we have a json object.
constexpr uint32_t kJBArray = 0x40000000; // indicates whether we have a json array.

uint32_t GetJEType(JEntry metadata) {
  static constexpr uint32_t kJETypeMask = 0xF0000000;
  return metadata & kJETypeMask;
}

uint32_t GetOffset(JEntry metadata) {
constexpr uint32_t kJEOffsetMask = 0x0FFFFFFF;
  return metadata & kJEOffsetMask;
}

uint32_t GetCount(JsonbHeader jsonb_header) {
  return jsonb_header & kJBCountMask;
}

std::string PrepareSerializedJsonb(const std::string& body) {
  common::Jsonb jsonb;
  auto s = jsonb.FromString(body);
  LOG_IF(DFATAL, !s.ok()) << "Unable to parse " << body;
  return s.ok() ? jsonb.SerializedJsonb() : "";
}

bool IsScalar(const JEntry& jentry) {
  uint32_t jentry_type = GetJEType(jentry);
  return ((jentry_type) != kJEIsArray && (jentry_type) != kJEIsObject);
}

size_t ComputeDataOffset(const size_t num_entries, const uint32_t container_type) {
  const size_t num_jentries = (container_type & kJBArray) ? num_entries : 2 * num_entries;
  return sizeof(JsonbHeader) + num_jentries * sizeof(JEntry);
}

} // namespace

string Jsonb::kSerializedJsonbNull = PrepareSerializedJsonb("null");
string Jsonb::kSerializedJsonbEmpty = PrepareSerializedJsonb("{}");

Jsonb::Jsonb() {
}

Jsonb::Jsonb(std::string_view jsonb) : serialized_jsonb_(jsonb) {}

Jsonb::Jsonb(const std::string& jsonb)
    : serialized_jsonb_(jsonb) {
}

Jsonb::Jsonb(std::string&& jsonb)
    : serialized_jsonb_(std::move(jsonb)) {
}

void Jsonb::Assign(std::string&& jsonb) {
  serialized_jsonb_ = std::move(jsonb);
}

std::string&& Jsonb::MoveSerializedJsonb() {
  return std::move(serialized_jsonb_);
}

const std::string& Jsonb::SerializedJsonb() const {
  return serialized_jsonb_;
}

bool Jsonb::operator==(const Jsonb& other) const {
  return serialized_jsonb_ == other.serialized_jsonb_;
}

Status Jsonb::FromString(const std::string& json) {
  // Parse the json document.
  rapidjson::Document document;
  document.Parse<0>(json.c_str());
  if (document.HasParseError()) {
    return STATUS(Corruption, "JSON text is corrupt",
                  rapidjson::GetParseError_En(document.GetParseError()));
  }
  return FromRapidJson(document);
}

Status Jsonb::FromRapidJson(const rapidjson::Document& document) {
  return ToJsonbInternal(document, &serialized_jsonb_);
}

Status Jsonb::FromRapidJson(const rapidjson::Value& value) {
  rapidjson::Document document;
  document.CopyFrom(value, document.GetAllocator());
  return FromRapidJson(document);
}

Status Jsonb::FromQLValue(const LWQLValuePB& value_pb) {
  return FromQLValue(value_pb.ToGoogleProtobuf());
}

Status Jsonb::FromQLValue(const QLValuePB& value_pb) {
  rapidjson::Document document;
  RETURN_NOT_OK(ConvertQLValuePBToRapidJson(value_pb, &document));
  return FromRapidJson(document);
}

Status Jsonb::FromQLValue(const QLValue& value) {
  return FromQLValue(value.value());
}

std::pair<size_t, size_t> Jsonb::ComputeOffsetsAndJsonbHeader(size_t num_entries,
                                                              uint32_t container_type,
                                                              std::string* jsonb) {
  // Compute the size we need to allocate for the metadata.
  size_t metadata_offset = jsonb->size();
  const size_t jsonb_metadata_size = ComputeDataOffset(num_entries, container_type);

  // Resize the string to fit the jsonb header and the jentry for keys and values.
  jsonb->resize(metadata_offset + jsonb_metadata_size);

  // Store the jsonb header at the appropriate place.
  JsonbHeader jsonb_header = GetCount(narrow_cast<JsonbHeader>(num_entries)) | container_type;
  BigEndian::Store32(&((*jsonb)[metadata_offset]), jsonb_header);
  metadata_offset += sizeof(JsonbHeader);

  return std::make_pair(metadata_offset, jsonb_metadata_size);
}

Status Jsonb::ToJsonbProcessObject(const rapidjson::Value& document,
                                   std::string* jsonb) {
  DCHECK(document.IsObject());

  // Use a map since we need to store the keys in sorted order.
  std::map<string, const rapidjson::Value&> kv_pairs;
  for (const auto& member : document.GetObject()) {
    kv_pairs.emplace(member.name.GetString(), member.value);
  }

  size_t metadata_offset, jsonb_metadata_size;
  std::tie(metadata_offset, jsonb_metadata_size) = ComputeOffsetsAndJsonbHeader(kv_pairs.size(),
                                                                                kJBObject, jsonb);

  // Now append the keys and store the key offsets in the jentry.
  size_t data_begin_offset = jsonb->size();
  for (const auto& entry : kv_pairs) {
    jsonb->append(entry.first);
    JEntry key_offset = narrow_cast<JEntry>(jsonb->size() - data_begin_offset);
    JEntry jentry = GetOffset(key_offset) | kJEIsString; // keys are always strings.
    BigEndian::Store32(&((*jsonb)[metadata_offset]), jentry);
    metadata_offset += sizeof(JEntry);
  }

  // Append the values to the buffer.
  for (const auto& entry : kv_pairs) {
    const rapidjson::Value& value = entry.second;
    RETURN_NOT_OK(ProcessJsonValueAndMetadata(value, data_begin_offset, jsonb, &metadata_offset));
  }

  // The metadata slice should now be empty.
  if (data_begin_offset != metadata_offset) {
    return STATUS(Corruption, "Couldn't process entire data for json object");
  }
  return Status::OK();
}

Status Jsonb::ProcessJsonValueAndMetadata(const rapidjson::Value& value,
                                          const size_t data_begin_offset,
                                          std::string* jsonb,
                                          size_t* metadata_offset) {
  JEntry jentry = 0;
  switch (value.GetType()) {
    case rapidjson::Type::kNullType:
      jentry |= kJEIsNull;
      break;
    case rapidjson::Type::kFalseType:
      jentry |= kJEIsBoolFalse;
      break;
    case rapidjson::Type::kTrueType:
      jentry |= kJEIsBoolTrue;
      break;
    case rapidjson::Type::kArrayType:
      jentry |= kJEIsArray;
      RETURN_NOT_OK(ToJsonbInternal(value, jsonb));
      break;
    case rapidjson::Type::kObjectType:
      jentry |= kJEIsObject;
      RETURN_NOT_OK(ToJsonbInternal(value, jsonb));
      break;
    case rapidjson::Type::kNumberType:
      if (value.IsInt()) {
        jentry |= kJEIsInt;
        util::AppendInt32ToKey(value.GetInt(), jsonb);
      } else if (value.IsUint()) {
        jentry |= kJEIsUInt;
        util::AppendBigEndianUInt32(value.GetUint(), jsonb);
      } else if (value.IsInt64()) {
        jentry |= kJEIsInt64;
        util::AppendInt64ToKey(value.GetInt64(), jsonb);
      } else if (value.IsUint64()) {
        jentry |= kJEIsUInt64;
        util::AppendBigEndianUInt64(value.GetUint64(), jsonb);
      } else if (value.IsFloat()) {
        jentry |= kJEIsFloat;
        util::AppendFloatToKey(value.GetFloat(), jsonb);
      } else if (value.IsDouble()) {
        jentry |= kJEIsDouble;
        util::AppendDoubleToKey(value.GetDouble(), jsonb);
      } else {
        return STATUS(NotSupported, "Numeric type is not supported");
      }
      break;
    case rapidjson::Type::kStringType:
      jentry |= kJEIsString;
      jsonb->append(value.GetString());
      break;
  }

  // Store the offset.
  auto offset = narrow_cast<JEntry>(jsonb->size() - data_begin_offset);
  jentry |= GetOffset(offset);

  // Store the JEntry.
  BigEndian::Store32(&((*jsonb)[*metadata_offset]), jentry);
  (*metadata_offset) += sizeof(JEntry);
  return Status::OK();
}

Status Jsonb::ToJsonbProcessArray(const rapidjson::Value& document,
                                  const bool is_scalar,
                                  std::string* jsonb) {
  DCHECK(document.IsArray());

  const auto& json_array = document.GetArray();
  const size_t num_array_entries = json_array.Size();

  uint32_t container_type = kJBArray;
  if (is_scalar) {
    // Scalars are an array with one element and the special kJBScalar field set in the header.
    DCHECK_EQ(num_array_entries, 1);
    container_type |= kJBScalar;
  }

  size_t metadata_offset, jsonb_metadata_size;
  std::tie(metadata_offset, jsonb_metadata_size) = ComputeOffsetsAndJsonbHeader(num_array_entries,
                                                                                container_type,
                                                                                jsonb);
  const size_t data_begin_offset = jsonb->size();
  // Append the array members to the buffer.
  for (const rapidjson::Value& value : json_array) {
    RETURN_NOT_OK(ProcessJsonValueAndMetadata(value, data_begin_offset, jsonb, &metadata_offset));
  }

  // The metadata slice should now be empty.
  if (data_begin_offset != metadata_offset) {
    return STATUS(Corruption, "Couldn't process entire data for json array");
  }
  return Status::OK();
}

Status Jsonb::ToJsonbInternal(const rapidjson::Value& document, std::string* jsonb) {
  if (document.IsObject()) {
    return ToJsonbProcessObject(document, jsonb);
  } else if (document.IsArray()) {
    return ToJsonbProcessArray(document, /* is_scalar */ false, jsonb);
  } else {
    // Scalar values are stored as an array with one element with a special field in the header
    // indicating it is a scalar.
    rapidjson::Document json_array;
    json_array.SetArray();

    rapidjson::Value tmpvalue;
    tmpvalue.CopyFrom(document, json_array.GetAllocator());
    json_array.PushBack(std::move(tmpvalue), json_array.GetAllocator());
    return ToJsonbProcessArray(json_array, true, jsonb);
  }
}

namespace {

rapidjson::Value ValueFromSlice(rapidjson::Document* document, const Slice& value) {
  return rapidjson::Value(
      value.cdata(), narrow_cast<rapidjson::SizeType>(value.size()), document->GetAllocator());
}

template <typename T>
void AddNumericMember(rapidjson::Document* document, const Slice& key, T value) {
  document->AddMember(
      ValueFromSlice(document, key), rapidjson::Value(value), document->GetAllocator());
}

template <typename T>
void PushBackNumericMember(rapidjson::Document* document, T value) {
  document->PushBack(rapidjson::Value(value),
                     document->GetAllocator());
}

#define DEFINE_SETTERS(lower_name, upper_name) \
  auto BOOST_PP_CAT(Set, upper_name)(QLValuePB* result) { \
    struct BOOST_PP_CAT(BOOST_PP_CAT(Set, upper_name), Impl) { \
      void operator()(std::string&& str) const { \
        result->BOOST_PP_CAT(set_, lower_name)(std::move(str)); \
      } \
      void operator()(Slice str) const { \
        result->BOOST_PP_CAT(set_, lower_name)(str.data(), str.size()); \
      } \
      void operator()(const char* str) const { \
        result->BOOST_PP_CAT(set_, lower_name)(str); \
      } \
      QLValuePB* result; \
    }; \
    return BOOST_PP_CAT(BOOST_PP_CAT(Set, upper_name), Impl){result}; \
  } \
  auto BOOST_PP_CAT(Set, upper_name)(LWQLValuePB* result) { \
    return [result](Slice value) { \
      result->BOOST_PP_CAT(dup_, lower_name)(value); \
    }; \
  }

DEFINE_SETTERS(string_value, StringValue);
DEFINE_SETTERS(jsonb_value, JsonbValue);

// Given a serialized json scalar and its metadata, return a string representation of it.
template <class F>
Status ScalarToString(const JEntry& element_metadata, Slice json_value, F f) {
  switch (GetJEType(element_metadata)) {
    case kJEIsString: {
      f(json_value);
      break;
    }
    case kJEIsInt: {
      int32_t value = util::DecodeInt32FromKey(json_value);
      f(std::to_string(value));
      break;
    }
    case kJEIsUInt: {
      uint32_t value = BigEndian::Load32(json_value.data());
      f(std::to_string(value));
      break;
    }
    case kJEIsInt64: {
      int64_t value = util::DecodeInt64FromKey(json_value);
      f(std::to_string(value));
      break;
    }
    case kJEIsUInt64: {
      uint64_t value = BigEndian::Load64(json_value.data());
      f(std::to_string(value));
      break;
    }
    case kJEIsDouble: {
      double value = util::DecodeDoubleFromKey(json_value);
      f(std::to_string(value));
      break;
    }
    case kJEIsFloat: {
      float value = util::DecodeFloatFromKey(json_value);
      f(std::to_string(value));
      break;
    }
    case kJEIsBoolFalse: {
      f("false");
      break;
    }
    case kJEIsBoolTrue: {
      f("true");
      break;
    }
    case kJEIsNull: {
      f("null");
      break;
    }
    case kJEIsObject: FALLTHROUGH_INTENDED;
    case kJEIsArray:
      return STATUS(InvalidArgument, "Arrays and Objects not supported for this method");
  }
  return Status::OK();
}

// Given a scalar value retrieved from a serialized jsonb, this method creates a jsonb scalar
// (which is a single element within an array). This is required for comparison purposes.
Result<std::string> CreateScalar(const Slice& scalar, const JEntry& original_jentry) {
  // Build the header.
  size_t metadata_begin_offset = sizeof(JsonbHeader);
  size_t metadata_size = metadata_begin_offset + sizeof(JEntry);
  size_t data_begin_offset = metadata_size;

  // Resize the result.
  std::string scalar_jsonb;
  scalar_jsonb.resize(metadata_size);
  scalar_jsonb.append(scalar.cdata(), scalar.size());

  JsonbHeader jsonb_header = (1 & kJBCountMask) | kJBArray | kJBScalar;
  JEntry jentry = GetOffset(narrow_cast<JEntry>(scalar_jsonb.size() - data_begin_offset))
                  | GetJEType(original_jentry);

  // Store the header.
  BigEndian::Store32(scalar_jsonb.data(), jsonb_header);
  // Store the JEntry.
  BigEndian::Store32(scalar_jsonb.data() + metadata_begin_offset, jentry);
  return scalar_jsonb;
}

// Helper method to retrieve the (offset, length) of a key/value serialized in jsonb format.
// element_metadata_offset denotes the offset for the JEntry of the key/value,
// element_end_offset denotes the end of data portion of the key/value, data_begin_offset
// denotes the offset from which the data portion of jsonb starts, metadata_begin_offset is the
// offset from which all the JEntry fields begin.
std::pair<size_t, size_t> GetOffsetAndLength(
    size_t element_metadata_offset, Slice jsonb, size_t element_end_offset,
    size_t data_begin_offset, size_t metadata_begin_offset) {
  if (element_metadata_offset == metadata_begin_offset) {
    // This is the first element.
    return std::make_pair(data_begin_offset, element_end_offset);
  }

  DCHECK_GE(element_metadata_offset, sizeof(JsonbHeader));
  JEntry prev_element =
      BigEndian::Load32(jsonb.data() + element_metadata_offset - sizeof(JEntry));
  size_t prev_element_offset = GetOffset(prev_element);
  return std::make_pair(prev_element_offset + data_begin_offset,
                        element_end_offset - prev_element_offset);
}

// Retrieves the value from a serialized jsonb object at the given index. The result is a
// slice pointing to a section of the serialized jsonb string provided. The parameters
// metdata_begin_offset and data_begin_offset indicate the starting positions of metadata and
// data in the serialized jsonb. The parameter num_kv_pairs indicates the total number of kv
// pairs in the json object. The method also returns a JEntry for the specified element, if
// metadata information for that element is required.
Status GetObjectValue(size_t index, Slice jsonb, size_t metadata_begin_offset,
                      size_t data_begin_offset, size_t num_kv_pairs, Slice *result,
                      JEntry* value_metadata) {
  // Compute the value index.
  size_t key_index = metadata_begin_offset + (index * sizeof(JEntry));
  size_t value_index = key_index + num_kv_pairs * sizeof(JEntry);
  if (value_index >= jsonb.size()) {
    return STATUS(Corruption, "value index in jsonb out of bounds");
  }

  // Read the value metadata.
  *value_metadata = BigEndian::Load32(jsonb.data() + value_index);

  // Read the value.
  size_t value_end_offset = GetOffset(*value_metadata);

  // Process the value.
  size_t value_offset;
  size_t value_length;
  std::tie(value_offset, value_length) = GetOffsetAndLength(value_index, jsonb, value_end_offset,
                                                            data_begin_offset,
                                                            metadata_begin_offset);
  if (value_offset + value_length > jsonb.size()) {
    return STATUS(Corruption, "json value data out of bounds in serialized jsonb");
  }

  *result = Slice(jsonb.data() + value_offset, value_length);
  return Status::OK();
}

// Retrieves the key from a serialized jsonb object at the given index. The result is a
// slice pointing to a section of the serialized jsonb string provided. The parameters
// metdata_begin_offset and data_begin_offset indicate the starting positions of metadata and
// data in the serialized jsonb.
Status GetObjectKey(
    size_t index, Slice jsonb, size_t metadata_begin_offset, size_t data_begin_offset,
    Slice *result) {
  // Compute the key index.
  size_t key_index = metadata_begin_offset + (index * sizeof(JEntry));
  if (key_index >= data_begin_offset) {
    return STATUS(Corruption, "key index in jsonb out of bounds");
  }

  // Read the key metadata.
  JEntry key_metadata = BigEndian::Load32(jsonb.data() + key_index);

  // Read the key.
  size_t key_end_offset = GetOffset(key_metadata);

  // Process the key.
  size_t key_offset;
  size_t key_length;
  std::tie(key_offset, key_length) = GetOffsetAndLength(key_index, jsonb, key_end_offset,
                                                        data_begin_offset, metadata_begin_offset);
  if (key_offset + key_length > jsonb.size()) {
    return STATUS(Corruption, "json key data out of bounds in serialized jsonb");
  }

  *result = Slice(jsonb.data() + key_offset, key_length);
  return Status::OK();
}

} // anonymous namespace

Status Jsonb::FromJsonbProcessObject(const Slice& jsonb,
                                     const JsonbHeader& jsonb_header,
                                     rapidjson::Document* document) {
  size_t metadata_begin_offset = sizeof(JsonbHeader);

  size_t nelems = GetCount(jsonb_header);
  const size_t data_begin_offset = ComputeDataOffset(nelems, kJBObject);

  // Now read the kv pairs and build the json.
  document->SetObject();
  for (size_t i = 0; i < nelems; i++) {
    Slice key;
    RETURN_NOT_OK(GetObjectKey(i, jsonb, metadata_begin_offset, data_begin_offset, &key));
    Slice json_value;
    JEntry value_metadata;
    RETURN_NOT_OK(GetObjectValue(i, jsonb, metadata_begin_offset, data_begin_offset, nelems,
                                 &json_value, &value_metadata));
    rapidjson::Value json_key = ValueFromSlice(document, key);
    switch (GetJEType(value_metadata)) {
      case kJEIsString: {
        document->AddMember(
            json_key, ValueFromSlice(document, json_value), document->GetAllocator());
        break;
      }
      case kJEIsInt: {
        int32_t value = util::DecodeInt32FromKey(json_value);
        AddNumericMember(document, key, value);
        break;
      }
      case kJEIsUInt: {
        uint32_t value = BigEndian::Load32(json_value.data());
        AddNumericMember(document, key, value);
        break;
      }
      case kJEIsInt64: {
        int64_t value = util::DecodeInt64FromKey(json_value);
        AddNumericMember(document, key, value);
        break;
      }
      case kJEIsUInt64: {
        uint64_t value = BigEndian::Load64(json_value.data());
        AddNumericMember(document, key, value);
        break;
      }
      case kJEIsDouble: {
        double value = util::DecodeDoubleFromKey(json_value);
        AddNumericMember(document, key, value);
        break;
      }
      case kJEIsFloat: {
        float value = util::DecodeFloatFromKey(json_value);
        AddNumericMember(document, key, value);
        break;
      }
      case kJEIsBoolFalse: {
        document->AddMember(json_key,
                            rapidjson::Value(false),
                            document->GetAllocator());
        break;
      }
      case kJEIsBoolTrue: {
        document->AddMember(json_key,
                            rapidjson::Value(true),
                            document->GetAllocator());
        break;
      }
      case kJEIsNull: {
        document->AddMember(json_key,
                            rapidjson::Value(rapidjson::Type::kNullType),
                            document->GetAllocator());
        break;
      }
      case kJEIsObject: {
        rapidjson::Document nested_container(&document->GetAllocator());
        nested_container.SetObject();
        RETURN_NOT_OK(FromJsonbInternal(json_value, &nested_container));
        document->AddMember(json_key,
                            std::move(nested_container),
                            document->GetAllocator());
        break;
      }
      case kJEIsArray: {
        rapidjson::Document nested_container(&document->GetAllocator());
        nested_container.SetArray();
        RETURN_NOT_OK(FromJsonbInternal(json_value, &nested_container));
        document->AddMember(json_key,
                            std::move(nested_container),
                            document->GetAllocator());
        break;
      }
    }
  }
  return Status::OK();
}

// Retrieves an element in serialized jsonb array with the provided index. The result is a
// slice pointing to a section of the serialized jsonb string provided. The parameters
// metdata_begin_offset and data_begin_offset indicate the starting positions of metadata and
// data in the serialized jsonb. The method also returns a JEntry for the specified element, if
// metadata information for that element is required.
Status GetArrayElement(
    size_t index, Slice jsonb, size_t metadata_begin_offset, size_t data_begin_offset,
    Slice* result, JEntry* element_metadata) {
  size_t value_index = metadata_begin_offset + (index * sizeof(JEntry));
  if (value_index >= jsonb.size()) {
    return STATUS(Corruption, "value index out of bounds");
  }

  // Read the metadata.
  *element_metadata = BigEndian::Load32(jsonb.data() + value_index);
  size_t value_end_offset = GetOffset(*element_metadata);

  // Process the value.
  size_t value_offset;
  size_t value_length;
  std::tie(value_offset, value_length) = GetOffsetAndLength(value_index, jsonb, value_end_offset,
                                                            data_begin_offset,
                                                            metadata_begin_offset);

  if (value_offset + value_length > jsonb.size()) {
    return STATUS(Corruption, "json value out of bounds of serialized jsonb");
  }
  *result = Slice(jsonb.data() + value_offset, value_length);
  return Status::OK();
}

Status Jsonb::FromJsonbProcessArray(const Slice& jsonb,
                                    const JsonbHeader& jsonb_header,
                                    rapidjson::Document* document) {

  size_t metadata_begin_offset = sizeof(JsonbHeader);
  size_t nelems = GetCount(jsonb_header);
  size_t data_begin_offset = ComputeDataOffset(nelems, kJBArray);

  // Now read the array members.
  document->SetArray();
  for (size_t i = 0; i < nelems; i++) {
    Slice result;
    JEntry element_metadata;
    RETURN_NOT_OK(GetArrayElement(i, jsonb, metadata_begin_offset, data_begin_offset, &result,
                                  &element_metadata));
    switch (GetJEType(element_metadata)) {
      case kJEIsString: {
        document->PushBack(ValueFromSlice(document, result), document->GetAllocator());
        break;
      }
      case kJEIsInt: {
        int32_t value = util::DecodeInt32FromKey(result);
        PushBackNumericMember(document, value);
        break;
      }
      case kJEIsUInt: {
        uint32_t value = BigEndian::Load32(result.data());
        PushBackNumericMember(document, value);
        break;
      }
      case kJEIsInt64: {
        int64_t value = util::DecodeInt64FromKey(result);
        PushBackNumericMember(document, value);
        break;
      }
      case kJEIsUInt64: {
        uint64_t value = BigEndian::Load64(result.data());
        PushBackNumericMember(document, value);
        break;
      }
      case kJEIsDouble: {
        double value = util::DecodeDoubleFromKey(result);
        PushBackNumericMember(document, value);
        break;
      }
      case kJEIsFloat: {
        float value = util::DecodeFloatFromKey(result);
        PushBackNumericMember(document, value);
        break;
      }
      case kJEIsBoolFalse: {
        document->PushBack(rapidjson::Value(false), document->GetAllocator());
        break;
      }
      case kJEIsBoolTrue: {
        document->PushBack(rapidjson::Value(true), document->GetAllocator());
        break;
      }
      case kJEIsNull: {
        document->PushBack(rapidjson::Value(rapidjson::Type::kNullType), document->GetAllocator());
        break;
      }
      case kJEIsObject: {
        rapidjson::Document nested_container(&document->GetAllocator());
        nested_container.SetObject();
        RETURN_NOT_OK(FromJsonbInternal(result, &nested_container));
        document->PushBack(std::move(nested_container),
                           document->GetAllocator());
        break;
      }
      case kJEIsArray: {
        rapidjson::Document nested_container(&document->GetAllocator());
        nested_container.SetArray();
        RETURN_NOT_OK(FromJsonbInternal(result, &nested_container));
        document->PushBack(std::move(nested_container),
                           document->GetAllocator());
        break;
      }
    }
  }
  return Status::OK();
}

Status Jsonb::FromJsonbInternal(const Slice& jsonb, rapidjson::Document* document) {
  // Read the jsonb header.
  JsonbHeader jsonb_header = BigEndian::Load32(jsonb.data());

  if ((jsonb_header & kJBObject) == kJBObject) {
    return FromJsonbProcessObject(jsonb, jsonb_header, document);
  }
  if ((jsonb_header & kJBArray) == kJBArray) {
    rapidjson::Document array_doc(&document->GetAllocator());
    RETURN_NOT_OK(FromJsonbProcessArray(jsonb, jsonb_header, &array_doc));

    if ((jsonb_header & kJBScalar) && array_doc.GetArray().Size() == 1) {
      // This is actually a scalar, since jsonb stores scalars as arrays with one element.
      // Therefore, just return the single element.
      document->CopyFrom(array_doc.GetArray()[0], document->GetAllocator());
    } else {
      document->CopyFrom(array_doc, document->GetAllocator());
    }
  } else {
    return STATUS(InvalidArgument, "Invalid json type!");
  }
  return Status::OK();
}

Status Jsonb::ToRapidJson(rapidjson::Document* document) const {
  return FromJsonbInternal(serialized_jsonb_, document);
}

Status Jsonb::ToJsonString(std::string* json) const {
  return ToJsonStringInternal(serialized_jsonb_, json);
}

Status Jsonb::ToJsonStringInternal(const Slice& jsonb, std::string* json) {
  rapidjson::Document document;
  RETURN_NOT_OK(FromJsonbInternal(jsonb, &document));
  *DCHECK_NOTNULL(json) = WriteRapidJsonToString(document);
  return Status::OK();
}

template <class Op>
Status ApplyJsonbOperatorToArray(
    Slice jsonb, const Op& json_op, const JsonbHeader& jsonb_header,
    Slice* result, JEntry* element_metadata) {
  if(!json_op.operand().value().has_varint_value()) {
    return STATUS_SUBSTITUTE(NotFound, "Couldn't apply json operator");
  }

  // For arrays, the argument needs to be an integer.
  size_t num_array_entries = GetCount(jsonb_header);

  // Retrieve the array index and verify.
  VarInt varint;
  RETURN_NOT_OK(varint.DecodeFromComparable(json_op.operand().value().varint_value()));
  int64_t array_index = VERIFY_RESULT(varint.ToInt64());

  if (array_index < 0 || implicit_cast<size_t>(array_index) >= num_array_entries) {
    return STATUS_SUBSTITUTE(NotFound, "Array index: $0 out of bounds [0, $1)",
                             array_index, num_array_entries);
  }

  RETURN_NOT_OK(GetArrayElement(array_index, jsonb, sizeof(jsonb_header),
                                ComputeDataOffset(num_array_entries, kJBArray), result,
                                element_metadata));
  return Status::OK();
}

template <class Op>
Status ApplyJsonbOperatorToObject(
    Slice jsonb, const Op& json_op, const JsonbHeader& jsonb_header,
    Slice* result, JEntry* element_metadata) {
  if (!json_op.operand().value().has_string_value()) {
    return STATUS_SUBSTITUTE(NotFound, "Couldn't apply json operator");
  }

  size_t num_kv_pairs = GetCount(jsonb_header);
  const auto& search_key = json_op.operand().value().string_value();

  size_t metadata_begin_offset = sizeof(jsonb_header);
  size_t data_begin_offset = ComputeDataOffset(num_kv_pairs, kJBObject);

  // Binary search to find the key.
  int64_t low = 0, high = num_kv_pairs - 1;
  auto search_key_slice = Slice(search_key);
  while (low <= high) {
    size_t mid = low + (high - low)/2;
    Slice mid_key;
    RETURN_NOT_OK(GetObjectKey(mid, jsonb, metadata_begin_offset, data_begin_offset, &mid_key));

    if (mid_key == search_key_slice) {
      RETURN_NOT_OK(GetObjectValue(mid, jsonb, sizeof(jsonb_header),
                                   ComputeDataOffset(num_kv_pairs, kJBObject), num_kv_pairs,
                                   result, element_metadata));
      return Status::OK();
    } else if (mid_key.ToBuffer() > search_key) {
      high = mid - 1;
    } else {
      low = mid + 1;
    }
  }
  return STATUS_FORMAT(NotFound, "Couldn't find key $0 in json document", search_key);
}

Status Jsonb::ApplyJsonbOperators(std::string_view serialized_json,
                                  const QLJsonColumnOperationsPB& json_ops,
                                  QLValuePB* result) {
  return DoApplyJsonbOperators(serialized_json, json_ops, result);
}

Status Jsonb::ApplyJsonbOperators(std::string_view serialized_json,
                                  const LWQLJsonColumnOperationsPB& json_ops,
                                  LWQLValuePB* result) {
  return DoApplyJsonbOperators(serialized_json, json_ops, result);
}

template <class Ops, class Value>
Status Jsonb::DoApplyJsonbOperators(
    std::string_view serialized_json, const Ops& json_ops, Value* result) {

  Slice jsonop_result;
  Slice operand(serialized_json);
  JEntry element_metadata;

  auto end_ops = json_ops.json_operations().end();
  for (auto it = json_ops.json_operations().begin(); it != end_ops;) {
    const Status s = ApplyJsonbOperator(operand, *it, &jsonop_result, &element_metadata);
    if (s.IsNotFound()) {
      // We couldn't apply the operator to the operand and hence return null as the result.
      SetNull(result);
      return Status::OK();
    }
    RETURN_NOT_OK(s);

    ++it;
    if (IsScalar(element_metadata) && it != end_ops) {
      // We have to apply another operation after this, but we received a scalar intermediate
      // result.
      SetNull(result);
      return Status::OK();
    }
    operand = jsonop_result;
  }

  // In case of '->>', we need to return a string result.
  if (!json_ops.json_operations().empty() &&
      (--end_ops)->json_operator() == JsonOperatorPB::JSON_TEXT) {
    if (IsScalar(element_metadata)) {
      RETURN_NOT_OK(ScalarToString(element_metadata, jsonop_result, SetStringValue(result)));
    } else {
      string str_result;
      RETURN_NOT_OK(ToJsonStringInternal(jsonop_result, &str_result));
      SetStringValue(result)(std::move(str_result));
    }
    return Status::OK();
  }

  auto setter = SetJsonbValue(result);
  if (IsScalar(element_metadata)) {
    // In case of a scalar that is received from an operation, convert it to a jsonb scalar.
    setter(VERIFY_RESULT(CreateScalar(jsonop_result, element_metadata)));
  } else {
    setter(jsonop_result);
  }
  return Status::OK();
}

template <class Op>
Status Jsonb::ApplyJsonbOperator(
    Slice jsonb, const Op& json_op, Slice* result, JEntry* element_metadata) {
  // Currently, both these operators are considered the same since we only handle strings.
  DCHECK(json_op.json_operator() == JsonOperatorPB::JSON_OBJECT ||
         json_op.json_operator() == JsonOperatorPB::JSON_TEXT);

  // We only support strings and integers as the argument to the json operation currently.
  DCHECK(json_op.operand().has_value());

  if (jsonb.size() < sizeof(JsonbHeader)) {
    return STATUS(InvalidArgument, "Not enough data to process");
  }

  JsonbHeader jsonb_header = BigEndian::Load32(jsonb.data());
  if ((jsonb_header & kJBScalar) && (jsonb_header & kJBArray)) {
    // This is a scalar value and no operators can be applied to it.
    return STATUS(NotFound, "Cannot apply operators to scalar values");
  } else if (jsonb_header & kJBArray) {
    return ApplyJsonbOperatorToArray(jsonb, json_op, jsonb_header, result, element_metadata);
  } else if (jsonb_header & kJBObject) {
    return ApplyJsonbOperatorToObject(jsonb, json_op, jsonb_header, result, element_metadata);
  }

  return STATUS(InvalidArgument, "Invalid json operation");
}

} // namespace common
} // namespace yb
