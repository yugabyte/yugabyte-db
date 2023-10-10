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
#include "yb/util/jsonreader.h"

#include <unordered_set>

#include <rapidjson/error/en.h>
#include <google/protobuf/message.h>

#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/strings/escaping.h"

#include "yb/util/logging.h"
#include "yb/util/status.h"

using google::protobuf::Descriptor;
using google::protobuf::EnumValueDescriptor;
using google::protobuf::FieldDescriptor;
using google::protobuf::Message;
using google::protobuf::Reflection;

using rapidjson::SizeType;
using rapidjson::Value;

using std::string;
using std::vector;
using std::unordered_set;
using strings::Substitute;

namespace yb {

JsonReader::JsonReader(string text) : text_(std::move(text)) {}

JsonReader::~JsonReader() {
}

Status JsonReader::Init() {
  document_.Parse<0>(text_.c_str());
  if (document_.HasParseError()) {
    return STATUS(Corruption, "JSON text is corrupt",
        rapidjson::GetParseError_En(document_.GetParseError()));
  }
  return Status::OK();
}

Status JsonReader::ExtractBool(const Value* object,
                               const char* field,
                               bool* result) const {
  const Value* val;
  RETURN_NOT_OK(ExtractField(object, field, &val));
  if (PREDICT_FALSE(!val->IsBool())) {
    return STATUS(InvalidArgument, Substitute(
        "Wrong type during field extraction: expected bool but got $0",
        val->GetType()));
  }
  *result = val->GetBool();
  return Status::OK();
}

Status JsonReader::ExtractInt32(const Value* object,
                                const char* field,
                                int32_t* result) const {
  const Value* val;
  RETURN_NOT_OK(ExtractField(object, field, &val));
  if (PREDICT_FALSE(!val->IsInt())) {
    return STATUS(InvalidArgument, Substitute(
        "Wrong type during field extraction: expected int32 but got $0",
        val->GetType()));
  }
  *result = val->GetUint();
  return Status::OK();
}

Status JsonReader::ExtractInt64(const Value* object,
                                const char* field,
                                int64_t* result) const {
  const Value* val;
  RETURN_NOT_OK(ExtractField(object, field, &val));
  if (PREDICT_FALSE(!val->IsInt64())) {
    return STATUS(InvalidArgument, Substitute(
        "Wrong type during field extraction: expected int64 but got $0",
        val->GetType()));
  }
  *result = val->GetInt64();
  return Status::OK();
}

Status JsonReader::ExtractUInt64(const Value* object,
                                 const char* field,
                                 uint64_t* result) const {
  const Value* val;
  RETURN_NOT_OK(ExtractField(object, field, &val));
  if (PREDICT_FALSE(!val->IsUint64())) {
    return STATUS(InvalidArgument, Substitute(
        "Wrong type during field extraction: expected uint64 but got $0",
        val->GetType()));
  }
  *result = val->GetUint64();
  return Status::OK();
}

Status JsonReader::ExtractString(const Value* object,
                                 const char* field,
                                 string* result) const {
  const Value* val;
  RETURN_NOT_OK(ExtractField(object, field, &val));
  if (PREDICT_FALSE(!val->IsString())) {
    if (val->IsNull()) {
      *result = "";
      return Status::OK();
    }
    return STATUS(InvalidArgument, Substitute(
        "Wrong type during field extraction: expected string but got $0",
        val->GetType()));  }
  result->assign(val->GetString());
  return Status::OK();
}

Status JsonReader::ExtractObject(const Value* object,
                                 const char* field,
                                 const Value** result) const {
  const Value* val = nullptr;
  RETURN_NOT_OK(ExtractField(object, field, &val));
  if (PREDICT_FALSE(!val->IsObject())) {
    return STATUS(InvalidArgument, Substitute(
        "Wrong type during field extraction: expected object but got $0",
        val->GetType()));  }
  *result = val;
  return Status::OK();
}

Status JsonReader::ExtractObjectArray(const Value* object,
                                      const char* field,
                                      vector<const Value*>* result) const {
  const Value* val;
  RETURN_NOT_OK(ExtractField(object, field, &val));
  if (PREDICT_FALSE(!val->IsArray())) {
    return STATUS(InvalidArgument, Substitute(
        "Wrong type during field extraction: expected object array but got $0",
        val->GetType()));  }
  for (Value::ConstValueIterator iter = val->Begin(); iter != val->End(); ++iter) {
    result->push_back(iter);
  }
  return Status::OK();
}

Status JsonReader::ExtractField(const Value* object,
                                const char* field,
                                const Value** result) const {
  if (field && PREDICT_FALSE(!object->HasMember(field))) {
    return STATUS(NotFound, "Missing field", field);
  }
  *result = field ? &(*object)[field] : object;
  return Status::OK();
}

Status JsonReader::ExtractProtobufField(const Value& value,
                                        Message* pb,
                                        const FieldDescriptor* field) const {
  const Reflection* const reflection = DCHECK_NOTNULL(DCHECK_NOTNULL(pb)->GetReflection());
  switch (DCHECK_NOTNULL(field)->cpp_type()) {
    case FieldDescriptor::CPPTYPE_INT32:
      reflection->SetInt32(pb, field, value.GetInt());
      break;
    case FieldDescriptor::CPPTYPE_INT64:
      reflection->SetInt64(pb, field, value.GetInt64());
      break;
    case FieldDescriptor::CPPTYPE_UINT32:
      reflection->SetUInt32(pb, field, value.GetUint());
      break;
    case FieldDescriptor::CPPTYPE_UINT64:
      reflection->SetUInt64(pb, field, value.GetUint64());
      break;
    case FieldDescriptor::CPPTYPE_DOUBLE:
      reflection->SetDouble(pb, field, value.GetDouble());
      break;
    case FieldDescriptor::CPPTYPE_FLOAT:
      reflection->SetFloat(pb, field, value.GetDouble());
      break;
    case FieldDescriptor::CPPTYPE_BOOL:
      reflection->SetBool(pb, field, value.GetBool());
      break;
    case FieldDescriptor::CPPTYPE_ENUM: {
        const EnumValueDescriptor* const val =
            DCHECK_NOTNULL(field->enum_type())->FindValueByName(value.GetString());
        SCHECK(val, InvalidArgument,
            Substitute("Cannot parse enum value $0", value.GetString()));
        reflection->SetEnum(pb, field, val);
      }
      break;
    case FieldDescriptor::CPPTYPE_STRING: {
        string unescaped, error;
        SCHECK(strings::CUnescape(value.GetString(), &unescaped, &error), InvalidArgument,
            Substitute("Cannot unescape string '$0' error: '$1'", value.GetString(), error));
        reflection->SetString(pb, field, unescaped);
      }
      break;
    case FieldDescriptor::CPPTYPE_MESSAGE:
      RETURN_NOT_OK(ExtractProtobufMessage(value, reflection->MutableMessage(pb, field)));
      break;
    default:
      return STATUS(NotSupported, Substitute("Unknown cpp_type $0", field->cpp_type()));
  }
  return Status::OK();
}

Status JsonReader::ExtractProtobufRepeatedField(const Value& value,
                                                Message* pb,
                                                const FieldDescriptor* field) const {
  const Reflection* const reflection = DCHECK_NOTNULL(DCHECK_NOTNULL(pb)->GetReflection());
  switch (DCHECK_NOTNULL(field)->cpp_type()) {
    case FieldDescriptor::CPPTYPE_INT32:
      reflection->AddInt32(pb, field, value.GetInt());
      break;
    case FieldDescriptor::CPPTYPE_INT64:
      reflection->AddInt64(pb, field, value.GetInt64());
      break;
    case FieldDescriptor::CPPTYPE_UINT32:
      reflection->AddUInt32(pb, field, value.GetUint());
      break;
    case FieldDescriptor::CPPTYPE_UINT64:
      reflection->AddUInt64(pb, field, value.GetUint64());
      break;
    case FieldDescriptor::CPPTYPE_DOUBLE:
      reflection->AddDouble(pb, field, value.GetDouble());
      break;
    case FieldDescriptor::CPPTYPE_FLOAT:
      reflection->AddFloat(pb, field, value.GetDouble());
      break;
    case FieldDescriptor::CPPTYPE_BOOL:
      reflection->AddBool(pb, field, value.GetBool());
      break;
    case FieldDescriptor::CPPTYPE_ENUM: {
        const EnumValueDescriptor* const val =
            DCHECK_NOTNULL(field->enum_type())->FindValueByName(value.GetString());
        SCHECK(val, InvalidArgument,
            Substitute("Cannot parse enum value $0", value.GetString()));
        reflection->AddEnum(pb, field, val);
      }
      break;
    case FieldDescriptor::CPPTYPE_STRING: {
        string unescaped, error;
        SCHECK(strings::CUnescape(value.GetString(), &unescaped, &error), InvalidArgument,
            Substitute("Cannot unescape string '$0' error: '$1'", value.GetString(), error));
        reflection->AddString(pb, field, unescaped);
      }
      break;
    case FieldDescriptor::CPPTYPE_MESSAGE:
      RETURN_NOT_OK(ExtractProtobufMessage(value, reflection->AddMessage(pb, field)));
      break;
    default:
      return STATUS(NotSupported, Substitute("Unknown cpp_type $0", field->cpp_type()));
  }
  return Status::OK();
}


Status JsonReader::ExtractProtobufMessage(const Value& value,
                                          Message* pb) const {
  const Descriptor* const desc = DCHECK_NOTNULL(DCHECK_NOTNULL(pb)->GetDescriptor());
  unordered_set<string> required;
  for (int i = 0; i < desc->field_count(); ++i) {
    const FieldDescriptor* const field = DCHECK_NOTNULL(desc->field(i));
    if (field->is_required()) {
      required.insert(field->name());
    }
  }

  for (Value::ConstMemberIterator it = value.MemberBegin(); it != value.MemberEnd(); ++it) {
    SCHECK(it->name.IsString(), InvalidArgument, "Expected String type of field name");
    const string name(it->name.GetString());
    if (!required.empty()) {
      required.erase(name);
    }

    const FieldDescriptor* const field = desc->FindFieldByName(name);
    SCHECK(field != nullptr, InvalidArgument,
        "Field '" + name + "' is not found in PB descriptor: " + desc->DebugString());

    if (field->is_repeated()) {
      SCHECK(it->value.IsArray(), InvalidArgument,
          "Expected JSON Array for field " + field->DebugString());
      for (Value::ConstValueIterator itr = it->value.Begin(); itr != it->value.End(); ++itr) {
        RETURN_NOT_OK(ExtractProtobufRepeatedField(*itr, pb, field));
      }
    } else {
      RETURN_NOT_OK(ExtractProtobufField(it->value, pb, field));
    }
  }

  SCHECK(required.empty(), InvalidArgument,
      "Absent required fields: " + ToString(required) + " in " + desc->DebugString());
  return Status::OK();
}

Status JsonReader::ExtractProtobuf(const Value* object,
                                   const char* field,
                                   Message* pb) const {
  const Value* value = nullptr;
  RETURN_NOT_OK(ExtractObject(object, field, &value));
  return ExtractProtobufMessage(*value, pb);
}

} // namespace yb
