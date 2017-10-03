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
#ifndef KUDU_UTIL_JSONWRITER_H
#define KUDU_UTIL_JSONWRITER_H

#include <inttypes.h>

#include <string>

#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/macros.h"

namespace google {
namespace protobuf {
class Message;
class FieldDescriptor;
} // namespace protobuf
} // namespace google

namespace kudu {

class JsonWriterIf;

// Acts as a pimpl for rapidjson so that not all metrics users must bring in the
// rapidjson library, which is template-based and therefore hard to forward-declare.
//
// This class implements all the methods of rapidjson::JsonWriter, plus an
// additional convenience method for String(std::string).
//
// We take an instance of std::stringstream in the constructor because Mongoose / Squeasel
// uses std::stringstream for output buffering.
class JsonWriter {
 public:
  enum Mode {
    // Pretty-print the JSON, with nice indentation, newlines, etc.
    PRETTY,
    // Print the JSON as compactly as possible.
    COMPACT
  };

  JsonWriter(std::stringstream* out, Mode mode);
  ~JsonWriter();

  void Null();
  void Bool(bool b);
  void Int(int i);
  void Uint(unsigned u);
  void Int64(int64_t i64);
  void Uint64(uint64_t u64);
  void Double(double d);
  void String(const char* str, size_t length);
  void String(const char* str);
  void String(const std::string& str);

  void Protobuf(const google::protobuf::Message& message);

  template<typename T>
  void Value(const T& val);

  void StartObject();
  void EndObject();
  void StartArray();
  void EndArray();

  // Convert the given protobuf to JSON format.
  static std::string ToJson(const google::protobuf::Message& pb,
                            Mode mode);

 private:
  void ProtobufField(const google::protobuf::Message& pb,
                     const google::protobuf::FieldDescriptor* field);
  void ProtobufRepeatedField(const google::protobuf::Message& pb,
                             const google::protobuf::FieldDescriptor* field,
                             int index);

  gscoped_ptr<JsonWriterIf> impl_;
  DISALLOW_COPY_AND_ASSIGN(JsonWriter);
};

} // namespace kudu

#endif // KUDU_UTIL_JSONWRITER_H
