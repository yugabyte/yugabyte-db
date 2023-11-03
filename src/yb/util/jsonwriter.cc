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
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
#include "yb/util/jsonwriter.h"

#include <string>

#include "yb/util/logging.h"
#include <google/protobuf/message.h>
#include <rapidjson/prettywriter.h>

#include "yb/gutil/casts.h"
#include "yb/gutil/strings/escaping.h"

using google::protobuf::FieldDescriptor;
using google::protobuf::Message;
using google::protobuf::Reflection;

using std::string;
using std::stringstream;
using std::vector;

namespace yb {

void JsonEscape(GStringPiece s, string* out) {
  out->reserve(out->size() + s.size() * 2);
  const char* p_end = s.data() + s.size();
  for (const char* p = s.data(); p != p_end; p++) {
    // Only the following characters need to be escaped, according to json.org.
    // In particular, it's illegal to escape the single-quote character, and
    // JSON does not support the "\x" escape sequence like C/Java.
    switch (*p) {
      case '"':
      case '\\':
        out->push_back('\\');
        out->push_back(*p);
        break;
      case '\b':
        out->append("\\b");
        break;
      case '\f':
        out->append("\\f");
        break;
      case '\n':
        out->append("\\n");
        break;
      case '\r':
        out->append("\\r");
        break;
      case '\t':
        out->append("\\t");
        break;
      default:
        out->push_back(*p);
    }
  }
}

// Adapter to allow RapidJSON to write directly to a stringstream.
// Since Squeasel exposes a stringstream as its interface, this is needed to avoid overcopying.
class UTF8StringStreamBuffer {
 public:
  typedef typename rapidjson::UTF8<>::Ch Ch;
  explicit UTF8StringStreamBuffer(std::stringstream* out);
  void Put(Ch c);

  void PutUnsafe(Ch c) { Put(c); }
  void Flush() {}
 private:
  std::stringstream* out_;
};

// rapidjson doesn't provide any common interface between the PrettyWriter and
// Writer classes. So, we create our own pure virtual interface here, and then
// use JsonWriterImpl<T> below to make the two different rapidjson implementations
// correspond to this subclass.
class JsonWriterIf {
 public:
  virtual void Null() = 0;
  virtual void Bool(bool b) = 0;
  virtual void Int(int i) = 0;
  virtual void Uint(unsigned u) = 0;
  virtual void Int64(int64_t i64) = 0;
  virtual void Uint64(uint64_t u64) = 0;
  virtual void Double(double d) = 0;
  virtual void String(const char* str, size_t length) = 0;
  virtual void String(const char* str) = 0;
  virtual void String(const std::string& str) = 0;

  virtual void StartObject() = 0;
  virtual void EndObject() = 0;
  virtual void StartArray() = 0;
  virtual void EndArray() = 0;

  virtual ~JsonWriterIf() {}
};

// Adapts the different rapidjson Writer implementations to our virtual
// interface above.
template<class T>
class JsonWriterImpl : public JsonWriterIf {
 public:
  explicit JsonWriterImpl(stringstream* out);

  void Null() override;
  void Bool(bool b) override;
  void Int(int i) override;
  void Uint(unsigned u) override;
  void Int64(int64_t i64) override;
  void Uint64(uint64_t u64) override;
  void Double(double d) override;
  void String(const char* str, size_t length) override;
  void String(const char* str) override;
  void String(const std::string& str) override;

  void StartObject() override;
  void EndObject() override;
  void StartArray() override;
  void EndArray() override;

 protected:
  UTF8StringStreamBuffer stream_;
  T writer_;
 private:
  DISALLOW_COPY_AND_ASSIGN(JsonWriterImpl);
};

template<class T>
class EscapedJsonWriterImpl : public JsonWriterImpl<T> {
 public:
  explicit EscapedJsonWriterImpl(stringstream* out) : JsonWriterImpl<T>(out) {}

  void String(const char* str, size_t length) override;
  void String(const char* str) override;
  void String(const std::string& str) override;
};

//
// JsonWriter
//

typedef rapidjson::PrettyWriter<UTF8StringStreamBuffer> PrettyWriterClass;
typedef rapidjson::Writer<UTF8StringStreamBuffer> CompactWriterClass;

JsonWriter::JsonWriter(stringstream* out, Mode m) {
  switch (m) {
    case PRETTY:
      impl_.reset(new JsonWriterImpl<PrettyWriterClass>(DCHECK_NOTNULL(out)));
      break;
    case COMPACT:
      impl_.reset(new JsonWriterImpl<CompactWriterClass>(DCHECK_NOTNULL(out)));
      break;
    case PRETTY_ESCAPE_STR:
      impl_.reset(new EscapedJsonWriterImpl<PrettyWriterClass>(DCHECK_NOTNULL(out)));
      break;
    case COMPACT_ESCAPE_STR:
      impl_.reset(new EscapedJsonWriterImpl<CompactWriterClass>(DCHECK_NOTNULL(out)));
      break;
  }
}
JsonWriter::~JsonWriter() {
}
void JsonWriter::Null() { impl_->Null(); }
void JsonWriter::Bool(bool b) { impl_->Bool(b); }
void JsonWriter::Int(int i) { impl_->Int(i); }
void JsonWriter::Uint(unsigned u) { impl_->Uint(u); }
void JsonWriter::Int64(int64_t i64) { impl_->Int64(i64); }
void JsonWriter::Uint64(uint64_t u64) { impl_->Uint64(u64); }
void JsonWriter::Double(double d) { impl_->Double(d); }
void JsonWriter::String(const char* str, size_t length) { impl_->String(str, length); }
void JsonWriter::String(const char* str) { impl_->String(str); }
void JsonWriter::String(const string& str) { impl_->String(str); }
void JsonWriter::StartObject() { impl_->StartObject(); }
void JsonWriter::EndObject() { impl_->EndObject(); }
void JsonWriter::StartArray() { impl_->StartArray(); }
void JsonWriter::EndArray() { impl_->EndArray(); }

// Specializations for common primitive metric types.
template<> void JsonWriter::Value(const bool& val) {
  Bool(val);
}
template<> void JsonWriter::Value(const int32_t& val) {
  Int(val);
}
template<> void JsonWriter::Value(const uint32_t& val) {
  Uint(val);
}
template<> void JsonWriter::Value(const int64_t& val) {
  Int64(val);
}
template<> void JsonWriter::Value(const uint64_t& val) {
  Uint64(val);
}
template<> void JsonWriter::Value(const double& val) {
  Double(val);
}
template<> void JsonWriter::Value(const string& val) {
  String(val);
}

#if defined(__APPLE__)
template<> void JsonWriter::Value(const size_t& val) {
  Uint64(val);
}
#endif

void JsonWriter::Protobuf(const Message& pb) {
  const Reflection* reflection = pb.GetReflection();
  vector<const FieldDescriptor*> fields;
  reflection->ListFields(pb, &fields);

  StartObject();
  for (const FieldDescriptor* field : fields) {
    String(field->name());
    if (field->is_repeated()) {
      StartArray();
      auto field_size = reflection->FieldSize(pb, field);
      for (int i = 0; i < field_size; i++) {
        ProtobufRepeatedField(pb, field, i);
      }
      EndArray();
    } else {
      ProtobufField(pb, field);
    }
  }
  EndObject();
}

void JsonWriter::ProtobufField(const Message& pb, const FieldDescriptor* field) {
  const Reflection* reflection = pb.GetReflection();
  switch (field->cpp_type()) {
    case FieldDescriptor::CPPTYPE_INT32:
      Int(reflection->GetInt32(pb, field));
      break;
    case FieldDescriptor::CPPTYPE_INT64:
      Int64(reflection->GetInt64(pb, field));
      break;
    case FieldDescriptor::CPPTYPE_UINT32:
      Uint(reflection->GetUInt32(pb, field));
      break;
    case FieldDescriptor::CPPTYPE_UINT64:
      Uint64(reflection->GetUInt64(pb, field));
      break;
    case FieldDescriptor::CPPTYPE_DOUBLE:
      Double(reflection->GetDouble(pb, field));
      break;
    case FieldDescriptor::CPPTYPE_FLOAT:
      Double(reflection->GetFloat(pb, field));
      break;
    case FieldDescriptor::CPPTYPE_BOOL:
      Bool(reflection->GetBool(pb, field));
      break;
    case FieldDescriptor::CPPTYPE_ENUM:
      String(reflection->GetEnum(pb, field)->name());
      break;
    case FieldDescriptor::CPPTYPE_STRING:
      String(reflection->GetString(pb, field));
      break;
    case FieldDescriptor::CPPTYPE_MESSAGE:
      Protobuf(reflection->GetMessage(pb, field));
      break;
    default:
      LOG(FATAL) << "Unknown cpp_type: " << field->cpp_type();
  }
}

void JsonWriter::ProtobufRepeatedField(const Message& pb, const FieldDescriptor* field, int index) {
  const Reflection* reflection = pb.GetReflection();
  switch (field->cpp_type()) {
    case FieldDescriptor::CPPTYPE_INT32:
      Int(reflection->GetRepeatedInt32(pb, field, index));
      break;
    case FieldDescriptor::CPPTYPE_INT64:
      Int64(reflection->GetRepeatedInt64(pb, field, index));
      break;
    case FieldDescriptor::CPPTYPE_UINT32:
      Uint(reflection->GetRepeatedUInt32(pb, field, index));
      break;
    case FieldDescriptor::CPPTYPE_UINT64:
      Uint64(reflection->GetRepeatedUInt64(pb, field, index));
      break;
    case FieldDescriptor::CPPTYPE_DOUBLE:
      Double(reflection->GetRepeatedDouble(pb, field, index));
      break;
    case FieldDescriptor::CPPTYPE_FLOAT:
      Double(reflection->GetRepeatedFloat(pb, field, index));
      break;
    case FieldDescriptor::CPPTYPE_BOOL:
      Bool(reflection->GetRepeatedBool(pb, field, index));
      break;
    case FieldDescriptor::CPPTYPE_ENUM:
      String(reflection->GetRepeatedEnum(pb, field, index)->name());
      break;
    case FieldDescriptor::CPPTYPE_STRING:
      String(reflection->GetRepeatedString(pb, field, index));
      break;
    case FieldDescriptor::CPPTYPE_MESSAGE:
      Protobuf(reflection->GetRepeatedMessage(pb, field, index));
      break;
    default:
      LOG(FATAL) << "Unknown cpp_type: " << field->cpp_type();
  }
}

string JsonWriter::ToJson(const Message& pb, Mode mode) {
  stringstream stream;
  JsonWriter writer(&stream, mode);
  writer.Protobuf(pb);
  return stream.str();
}

//
// UTF8StringStreamBuffer
//

UTF8StringStreamBuffer::UTF8StringStreamBuffer(std::stringstream* out)
  : out_(DCHECK_NOTNULL(out)) {
}

void UTF8StringStreamBuffer::Put(rapidjson::UTF8<>::Ch c) {
  out_->put(c);
}

//
// JsonWriterImpl: simply forward to the underlying implementation.
//

template<class T>
JsonWriterImpl<T>::JsonWriterImpl(stringstream* out)
  : stream_(DCHECK_NOTNULL(out)),
    writer_(stream_) {
}
template<class T>
void JsonWriterImpl<T>::Null() { writer_.Null(); }
template<class T>
void JsonWriterImpl<T>::Bool(bool b) { writer_.Bool(b); }
template<class T>
void JsonWriterImpl<T>::Int(int i) { writer_.Int(i); }
template<class T>
void JsonWriterImpl<T>::Uint(unsigned u) { writer_.Uint(u); }
template<class T>
void JsonWriterImpl<T>::Int64(int64_t i64) { writer_.Int64(i64); }
template<class T>
void JsonWriterImpl<T>::Uint64(uint64_t u64) { writer_.Uint64(u64); }
template<class T>
void JsonWriterImpl<T>::Double(double d) { writer_.Double(d); }

template<class T>
void JsonWriterImpl<T>::String(const char* str, size_t length) {
  writer_.String(str, narrow_cast<rapidjson::SizeType>(length));
}

template<class T>
void JsonWriterImpl<T>::String(const char* str) { writer_.String(str); }
template<class T>
void JsonWriterImpl<T>::String(const string& str) { String(str.c_str(), str.length()); }
template<class T>
void JsonWriterImpl<T>::StartObject() { writer_.StartObject(); }
template<class T>
void JsonWriterImpl<T>::EndObject() { writer_.EndObject(); }
template<class T>
void JsonWriterImpl<T>::StartArray() { writer_.StartArray(); }
template<class T>
void JsonWriterImpl<T>::EndArray() { writer_.EndArray(); }

//
// EscapedJsonWriterImpl: implementation with escaping non-printable characters in strings.
//

template<class T>
void EscapedJsonWriterImpl<T>::String(const string& str) {
  const string escaped = CHexEscape(str);
  JsonWriterImpl<T>::writer_.String(
      escaped.c_str(), narrow_cast<rapidjson::SizeType>(escaped.length()));
}

template<class T>
void EscapedJsonWriterImpl<T>::String(const char* str, size_t length) {
  String(string(str, length));
}

template<class T>
void EscapedJsonWriterImpl<T>::String(const char* str) {
  String(string(str));
}

} // namespace yb
