// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include <stdint.h>
#include <string>
#include <vector>

#include <rapidjson/document.h>

#include "yb/gutil/macros.h"

#include "yb/util/status_fwd.h"

namespace google {
namespace protobuf {
class FieldDescriptor;
class Message;
} // namespace protobuf
} // namespace google

namespace yb {

// Wraps the JSON parsing functionality of rapidjson::Document.
//
// Unlike JsonWriter, this class does not hide rapidjson internals from
// clients. That's because there's just no easy way to implement object and
// array parsing otherwise. At most, this class aspires to be a simpler
// error-handling wrapper for reading and parsing.
class JsonReader {
 public:
  explicit JsonReader(std::string text);
  virtual ~JsonReader();

  Status Init();

  // Extractor methods.
  //
  // If 'field' is not NULL, will look for a field with that name in the
  // given object, returning Status::NotFound if it cannot be found. If
  // 'field' is NULL, will try to convert 'object' directly into the
  // desire type.

  Status ExtractBool(const rapidjson::Value* object,
                      const char* field,
                      bool* result) const;

  Status ExtractInt32(const rapidjson::Value* object,
                      const char* field,
                      int32_t* result) const;

  Status ExtractInt64(const rapidjson::Value* object,
                      const char* field,
                      int64_t* result) const;

  Status ExtractUInt32(const rapidjson::Value* object,
                       const char* field,
                       uint32_t* result) const;

  Status ExtractUInt64(const rapidjson::Value* object,
                       const char* field,
                       uint64_t* result) const;

  Status ExtractString(const rapidjson::Value* object,
                       const char* field,
                       std::string* result) const;

  // 'result' is only valid for as long as JsonReader is alive.
  Status ExtractObject(const rapidjson::Value* object,
                       const char* field,
                       const rapidjson::Value** result) const;

  // 'result' is only valid for as long as JsonReader is alive.
  Status ExtractObjectArray(const rapidjson::Value* object,
                            const char* field,
                            std::vector<const rapidjson::Value*>* result) const;

  Status ExtractProtobuf(const rapidjson::Value* object,
                         const char* field,
                         google::protobuf::Message* pb) const;

  const rapidjson::Value* root() const { return &document_; }

 protected:
  Status ExtractField(const rapidjson::Value* object,
                      const char* field,
                      const rapidjson::Value** result) const;

  Status ExtractProtobufMessage(const rapidjson::Value& value,
                                google::protobuf::Message* pb) const;

  Status ExtractProtobufField(const rapidjson::Value& value,
                              google::protobuf::Message* pb,
                              const google::protobuf::FieldDescriptor* field) const;

  virtual Status ExtractProtobufRepeatedField(const rapidjson::Value& value,
                                              google::protobuf::Message* pb,
                                              const google::protobuf::FieldDescriptor* field) const;

 private:
  std::string text_;
  rapidjson::Document document_;

  DISALLOW_COPY_AND_ASSIGN(JsonReader);
};

} // namespace yb
