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

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

#include "yb/docdb/doc_kv_util.h"
#include "yb/docdb/jsonb.h"

namespace yb {
namespace docdb {

Status Jsonb::ToJsonb(const std::string& json, std::string* jsonb) {
  // Parse the json document.
  rapidjson::Document document;
  document.Parse<0>(json.c_str());
  if (document.HasParseError()) {
    return STATUS(Corruption, "JSON text is corrupt",
                  rapidjson::GetParseError_En(document.GetParseError()));
  }
  return ToJsonbInternal(document, jsonb);
}

CHECKED_STATUS Jsonb::ToJsonbInternal(const rapidjson::Value& document, std::string* jsonb) {
  // Use a map since we need to store the keys in sorted order.
  std::map<string, const rapidjson::Value&> kv_pairs;
  for (const auto& member : document.GetObject()) {
    kv_pairs.emplace(member.name.GetString(), member.value);
  }

  // Compute the size we need to allocate for the metadata.
  size_t metadata_offset = jsonb->size();
  size_t jsonb_metadata_size = sizeof(JsonbHeader) + 2 * kv_pairs.size() * sizeof(JEntry);

  // Resize the string to fit the jsonb header and the jentry for keys and values.
  jsonb->resize(jsonb->size() + jsonb_metadata_size);

  // Store the jsonb header at the appropriate place.
  JsonbHeader jsonb_header = (kv_pairs.size() & kJBCountMask) | kJBObject;
  BigEndian::Store32(&((*jsonb)[metadata_offset]), jsonb_header);
  metadata_offset += sizeof(JsonbHeader);

  // Now append the keys and values and store the offsets.
  vector<size_t> key_offsets;
  vector<size_t> value_offsets;
  size_t data_begin_offset = jsonb->size();
  for (const auto& entry : kv_pairs) {
    jsonb->append(entry.first);
    key_offsets.push_back(jsonb->size() - data_begin_offset);
  }

  // Append the values to the buffer.
  for (const auto& entry : kv_pairs) {
    const rapidjson::Value& value = entry.second;
    switch (value.GetType()) {
      case rapidjson::Type::kNullType:
        // Nothing to do here.
        break;
      case rapidjson::Type::kArrayType:
        return STATUS_SUBSTITUTE(NotSupported, "Arrays not yet supported in jsonb");
      case rapidjson::Type::kFalseType:
        // Nothing to do here.
        break;
      case rapidjson::Type::kTrueType:
        // Nothing to do here.
        break;
      case rapidjson::Type::kObjectType:
        RETURN_NOT_OK(ToJsonbInternal(value, jsonb));
        break;
      case rapidjson::Type::kNumberType:
        if (value.IsInt()) {
          AppendInt32ToKey(value.GetInt(), jsonb);
        } else if (value.IsUint()) {
          AppendBigEndianUInt32(value.GetUint(), jsonb);
        } else if (value.IsInt64()) {
          AppendInt64ToKey(value.GetInt64(), jsonb);
        } else if (value.IsUint64()) {
          AppendBigEndianUInt64(value.GetUint64(), jsonb);
        } else if (value.IsFloat()) {
          AppendFloatToKey(value.GetFloat(), jsonb);
        } else if (value.IsDouble()) {
          AppendDoubleToKey(value.GetDouble(), jsonb);
        } else {
          return STATUS(NotSupported, "Numeric type is not supported");
        }
        break;
      case rapidjson::Type::kStringType:
        jsonb->append(value.GetString());
        break;
    }
    value_offsets.push_back(jsonb->size() - data_begin_offset);
  }

  // Now fill in the JEntry for the keys.
  for (const auto& key_offset : key_offsets) {
    JEntry jentry = (key_offset & kJEOffsetMask) | kJEIsString; // keys are always strings.
    BigEndian::Store32(&((*jsonb)[metadata_offset]), jentry);
    metadata_offset += sizeof(JEntry);
  }

  // Fill in the JEntry for all the values.
  DCHECK_EQ(kv_pairs.size(), value_offsets.size());
  int index = 0;
  for (const auto& entry : kv_pairs) {
    const rapidjson::Value& value = entry.second;
    JEntry jentry = value_offsets[index] & kJEOffsetMask;
    switch (entry.second.GetType()) {
      case rapidjson::Type::kNullType:
        jentry |= kJEIsNull;
        break;
      case rapidjson::Type::kArrayType:
        jentry |= kJEIsContainer;
        break;
      case rapidjson::Type::kFalseType:
        jentry |= kJEIsBoolFalse;
        break;
      case rapidjson::Type::kTrueType:
        jentry |= kJEIsBoolTrue;
        break;
      case rapidjson::Type::kObjectType:
        jentry |= kJEIsContainer;
        break;
      case rapidjson::Type::kNumberType:
        if (value.IsInt()) {
          jentry |= kJEIsInt;
        } else if (value.IsUint()) {
          jentry |= kJEIsUInt;
        } else if (value.IsInt64()) {
          jentry |= kJEIsInt64;
        } else if (value.IsUint64()) {
          jentry |= kJEIsUInt64;
        } else if (value.IsFloat()) {
          jentry |= kJEIsFloat;
        } else if (value.IsDouble()) {
          jentry |= kJEIsDouble;
        } else {
          return STATUS(NotSupported, "Numeric type is not supported");
        }
        break;
      case rapidjson::Type::kStringType:
        jentry |= kJEIsString;
        break;
    }
    index++;

    // Store the JEntry.
    BigEndian::Store32(&((*jsonb)[metadata_offset]), jentry);
    metadata_offset += sizeof(JEntry);
  }

  // The metadata slice should now be empty.
  DCHECK_EQ(data_begin_offset, metadata_offset);
  return Status::OK();
}

namespace {

template <typename T>
void AddNumericMember(rapidjson::Document* document, const string& key, T value) {
  document->AddMember(rapidjson::Value(key.c_str(), key.size(), document->GetAllocator()),
                      rapidjson::Value(value),
                      document->GetAllocator());
}
} // anonymous namespace

Status Jsonb::FromJsonbInternal(const std::string& jsonb, size_t offset,
                                rapidjson::Document* document) {
  DCHECK_LT(offset, jsonb.size());

  // Read the jsonb header.
  JsonbHeader jsonb_header = BigEndian::Load32(&(jsonb[offset]));
  size_t metadata_begin_offset = offset + sizeof(JsonbHeader);

  size_t nelems = jsonb_header & kJBCountMask;
  size_t data_begin_offset = metadata_begin_offset + 2 * nelems * sizeof(JEntry);

  // Now read the kv pairs and build the json.
  document->SetObject();
  for (int i = 0; i < nelems; i++) {
    // Compute the key and value indexes.
    size_t key_index = metadata_begin_offset + (i * sizeof(JEntry));
    size_t value_index = key_index + nelems * sizeof(JEntry);
    DCHECK_LT(key_index, jsonb.size());
    DCHECK_LT(value_index, jsonb.size());

    // Read the kv metadata.
    JEntry key_metadata = BigEndian::Load32(&(jsonb[key_index]));
    JEntry value_metadata = BigEndian::Load32(&(jsonb[value_index]));

    // Read the keys and values.
    size_t key_end_offset = key_metadata & kJEOffsetMask;
    size_t value_end_offset = value_metadata & kJEOffsetMask;

    // Process the key.
    size_t key_offset;
    size_t key_length;
    std::tie(key_offset, key_length) = GetOffsetAndLength(key_index, jsonb, key_end_offset,
                                                          data_begin_offset, metadata_begin_offset);
    DCHECK_LE(key_offset + key_length, jsonb.size());
    const std::string& key = jsonb.substr(key_offset, key_length);

    // Process the value.
    size_t value_offset;
    size_t value_length;
    std::tie(value_offset, value_length) = GetOffsetAndLength(value_index, jsonb, value_end_offset,
                                                              data_begin_offset,
                                                              metadata_begin_offset);
    DCHECK_LE(value_offset + value_length, jsonb.size());

    rapidjson::Value json_key(key.c_str(), key.size(), document->GetAllocator());
    switch (value_metadata & kJETypeMask) {
      case kJEIsString: {
        const std::string &value = jsonb.substr(value_offset, value_length);
        document->AddMember(json_key,
                            rapidjson::Value(value.c_str(), value.size(), document->GetAllocator()),
                            document->GetAllocator());
        break;
      }
      case kJEIsInt: {
        int32_t value = DecodeInt32FromKey(&jsonb[value_offset]);
        AddNumericMember(document, key, value);
        break;
      }
      case kJEIsUInt: {
        uint32_t value = BigEndian::Load32(&jsonb[value_offset]);
        AddNumericMember(document, key, value);
        break;
      }
      case kJEIsInt64: {
        int64_t value = DecodeInt64FromKey(&jsonb[value_offset]);
        AddNumericMember(document, key, value);
        break;
      }
      case kJEIsUInt64: {
        uint64_t value = BigEndian::Load64(&jsonb[value_offset]);
        AddNumericMember(document, key, value);
        break;
      }
      case kJEIsDouble: {
        double value = DecodeDoubleFromKey(&jsonb[value_offset]);
        AddNumericMember(document, key, value);
        break;
      }
      case kJEIsFloat: {
        float value = DecodeFloatFromKey(&jsonb[value_offset]);
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
      case kJEIsContainer: {
        rapidjson::Document nested_container(&document->GetAllocator());
        nested_container.SetObject();
        RETURN_NOT_OK(FromJsonbInternal(jsonb, value_offset, &nested_container));
        document->AddMember(json_key,
                            std::move(nested_container),
                            document->GetAllocator());
        break;
      }
    }
  }
  return Status::OK();
}

pair<size_t, size_t> Jsonb::GetOffsetAndLength(size_t element_metadata_offset,
                                               const std::string& jsonb,
                                               size_t element_end_offset,
                                               size_t data_begin_offset,
                                               size_t metadata_begin_offset) {
  if (element_metadata_offset == metadata_begin_offset) {
    // This is the first element.
    return std::make_pair(data_begin_offset, element_end_offset);
  }

  DCHECK_GE(element_metadata_offset, sizeof(JEntry));
  JEntry prev_element = BigEndian::Load32(&(jsonb[element_metadata_offset - sizeof(JEntry)]));
  size_t prev_element_offset = prev_element & kJEOffsetMask;
  return std::make_pair(prev_element_offset + data_begin_offset,
                        element_end_offset - prev_element_offset);

}

Status Jsonb::FromJsonb(const std::string& jsonb, rapidjson::Document* document) {
  return FromJsonbInternal(jsonb, 0, document);
}

} // namespace docdb
} // namespace yb
