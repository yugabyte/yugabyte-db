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

#pragma once

#include <string>
#include <string_view>

#include <rapidjson/document.h>

#include "yb/util/status.h"

namespace yb {

class QLValuePB;

namespace common {

Status ConvertQLValuePBToRapidJson(const QLValuePB& value_pb,
                                   rapidjson::Value* value,
                                   rapidjson::Document::AllocatorType* alloc);

inline Status ConvertQLValuePBToRapidJson(const QLValuePB& value_pb,
                                          rapidjson::Document* document) {
  return ConvertQLValuePBToRapidJson(value_pb, document, &document->GetAllocator());
}

std::string WriteRapidJsonToString(const rapidjson::Value& value);
std::string PrettyWriteRapidJsonToString(const rapidjson::Value& document);

template <typename JsonObject>
void AddMember(
    rapidjson::Document::StringRefType name, const std::string& string_val, JsonObject* object,
    rapidjson::Document::AllocatorType* allocator) {
  rapidjson::Value val;
  val.SetString(
      string_val.c_str(), static_cast<rapidjson::SizeType>(string_val.length()), *allocator);
  object->AddMember(name, val, *allocator);
}

Result<rapidjson::Document> ParseJson(const std::string_view& raw);

Result<const rapidjson::Value&> GetMember(const rapidjson::Value& root, const char* name);
Result<std::string_view> GetMemberAsStr(const rapidjson::Value& root, const char* name);
Result<rapidjson::Value::ConstArray> GetMemberAsArray(
    const rapidjson::Value& root, const char* name);

} // namespace common
} // namespace yb
