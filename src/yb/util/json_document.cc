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

#include "yb/util/json_document.h"

#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "yb/util/result.h"
#include "yb/util/status.h"

namespace yb {

struct JsonValueImpl : public rapidjson::Value {
  // No added members. This aliasing subclass lets us cast between JsonValueImpl and
  // rapidjson::Value while keeping the header free of rapidjson. Using inheritance avoids undefined
  // behavior with strict aliasing when casting.
  using rapidjson::Value::Value; // inherit constructors
};

namespace {

// Cast helpers between const JsonValueImpl* and const rapidjson::Value*.
inline const rapidjson::Value* AsValue(const JsonValueImpl* impl) {
  return impl;
}

inline Result<const rapidjson::Value*> AsCheckedValue(const JsonValueImpl* impl,
                                                      const std::string& path) {
  if (impl) {
    return AsValue(impl);
  }
  return STATUS_FORMAT(NotFound, "missing node at path: $0", path);
}

inline const JsonValueImpl* AsValueImpl(const rapidjson::Value* value) {
  return static_cast<const JsonValueImpl*>(value);
}

} // namespace

// ---------------- JsonDocument implementation ----------------

struct JsonDocument::Impl {
  rapidjson::Document doc;
  bool parsed = false;
};

JsonDocument::JsonDocument() : impl_(std::make_unique<Impl>()) {}
JsonDocument::~JsonDocument() = default;

JsonDocument::JsonDocument(JsonDocument&&) noexcept = default;
JsonDocument& JsonDocument::operator=(JsonDocument&&) noexcept = default;

Result<JsonValue> JsonDocument::Parse(const char* json) {
  impl_->doc.Parse(json);
  impl_->parsed = !impl_->doc.HasParseError();
  SCHECK_FORMAT(impl_->parsed, InvalidArgument,
                "JSON parse error: $0", rapidjson::GetParseError_En(impl_->doc.GetParseError()));
  return Root();
}

Result<JsonValue> JsonDocument::Parse(std::string_view json) {
  // Expect null-terminated buffer.  This works for string literals and std::string::c_str().
  DCHECK_EQ(json.data()[json.size()], '\0');
  return Parse(json.data());
}

// Root accessor
JsonValue JsonDocument::Root() {
  if (!impl_->parsed) {
    return JsonValue();
  }
  return JsonValue(AsValueImpl(static_cast<const rapidjson::Value*>(&impl_->doc)), "");
}

namespace {

// Path helpers
std::string MakeElemPath(const std::string& base, size_t idx) {
  if (base.empty()) {
    return ".[" + std::to_string(idx) + "]";
  }
  return base + "[" + std::to_string(idx) + "]";
}

std::string MakeMemberPath(const std::string& base, const char* key) {
  if (base.empty()) {
    return "." + std::string(key);
  }
  return base + "." + key;
}

// Helper to create an error Status for type mismatch.
Status MakeTypeErrorStatus(const std::string& expected_type, const std::string& path) {
  return STATUS(InvalidArgument, "JSON type error", "expected " + expected_type + " at: " + path);
}

} // namespace

// ---------------- JsonValue implementation ----------------

JsonValue::JsonValue() : impl_(nullptr), path_("") {}
JsonValue::~JsonValue() = default;

JsonValue::JsonValue(const JsonValueImpl* impl, std::string path)
  : impl_(impl), path_(std::move(path)) {}

// Indexed access
JsonValue JsonValue::operator[](size_t index) const {
  const rapidjson::Value* v = AsValue(impl_);
  std::string elem_path = MakeElemPath(path_, index);
  if (!v || !v->IsArray()) {
    // Defer error until getter; return invalid node with updated path.
    return JsonValue(nullptr, elem_path);
  }
  if (index >= v->Size()) {
    return JsonValue(nullptr, elem_path);
  }
  const rapidjson::Value& elem = (*v)[static_cast<rapidjson::SizeType>(index)];
  return JsonValue(AsValueImpl(&elem), elem_path);
}

// Member access
JsonValue JsonValue::operator[](std::string_view key) const {
  return LookupMember(impl_, key, path_);
}

// Type queries
bool JsonValue::IsNull() const noexcept {
  const rapidjson::Value* v = AsValue(impl_);
  return v && v->IsNull();
}
bool JsonValue::IsBool() const noexcept {
  const rapidjson::Value* v = AsValue(impl_);
  return v && v->IsBool();
}
bool JsonValue::IsObject() const noexcept {
  const rapidjson::Value* v = AsValue(impl_);
  return v && v->IsObject();
}
bool JsonValue::IsArray() const noexcept {
  const rapidjson::Value* v = AsValue(impl_);
  return v && v->IsArray();
}
bool JsonValue::IsNumber() const noexcept {
  const rapidjson::Value* v = AsValue(impl_);
  return v && v->IsNumber();
}
bool JsonValue::IsInt32() const noexcept {
  const rapidjson::Value* v = AsValue(impl_);
  return v && v->IsInt();
}
bool JsonValue::IsUint32() const noexcept {
  const rapidjson::Value* v = AsValue(impl_);
  return v && v->IsUint();
}
bool JsonValue::IsInt64() const noexcept {
  const rapidjson::Value* v = AsValue(impl_);
  return v && v->IsInt64();
}
bool JsonValue::IsUint64() const noexcept {
  const rapidjson::Value* v = AsValue(impl_);
  return v && v->IsUint64();
}
bool JsonValue::IsFloat() const noexcept {
  const rapidjson::Value* v = AsValue(impl_);
  return v && v->IsFloat();
}
bool JsonValue::IsDouble() const noexcept {
  const rapidjson::Value* v = AsValue(impl_);
  return v && v->IsDouble();
}
bool JsonValue::IsString() const noexcept {
  const rapidjson::Value* v = AsValue(impl_);
  return v && v->IsString();
}

Result<bool> JsonValue::GetBool() const {
  const rapidjson::Value* v = VERIFY_RESULT(AsCheckedValue(impl_, path_));
  if (!v->IsBool()) {
    return MakeTypeErrorStatus("bool", path_);
  }
  return v->GetBool();
}

Result<JsonObject> JsonValue::GetObject() const {
  const rapidjson::Value* v = VERIFY_RESULT(AsCheckedValue(impl_, path_));
  if (!IsObject()) {
    return MakeTypeErrorStatus("object", path_);
  }
  return JsonObject(impl_, v->MemberCount(), path_);
}

Result<JsonArray> JsonValue::GetArray() const {
  const rapidjson::Value* v = VERIFY_RESULT(AsCheckedValue(impl_, path_));
  if (!v->IsArray()) {
    return MakeTypeErrorStatus("array", path_);
  }
  return JsonArray(impl_, v->Size(), path_);
}

Result<int32_t> JsonValue::GetInt32() const {
  const rapidjson::Value* v = VERIFY_RESULT(AsCheckedValue(impl_, path_));
  if (!v->IsInt()) {
    return MakeTypeErrorStatus("int32", path_);
  }
  return v->GetInt();
}

Result<uint32_t> JsonValue::GetUint32() const {
  const rapidjson::Value* v = VERIFY_RESULT(AsCheckedValue(impl_, path_));
  if (!v->IsUint()) {
    return MakeTypeErrorStatus("uint32", path_);
  }
  return v->GetUint();
}

Result<int64_t> JsonValue::GetInt64() const {
  const rapidjson::Value* v = VERIFY_RESULT(AsCheckedValue(impl_, path_));
  if (!v->IsInt64()) {
    return MakeTypeErrorStatus("int64", path_);
  }
  return v->GetInt64();
}

Result<uint64_t> JsonValue::GetUint64() const {
  const rapidjson::Value* v = VERIFY_RESULT(AsCheckedValue(impl_, path_));
  if (!v->IsUint64()) {
    return MakeTypeErrorStatus("uint64", path_);
  }
  return v->GetUint64();
}

Result<float> JsonValue::GetFloat() const {
  const rapidjson::Value* v = VERIFY_RESULT(AsCheckedValue(impl_, path_));
  if (!v->IsNumber()) {
    return MakeTypeErrorStatus("float", path_);
  }
  // Promote to double to check magnitude reliably before narrowing to float.
  double d = v->GetDouble();
  // Parse flags to accept nonstandard JSON were not set, so nan/inf is not expected.
  DCHECK(!(std::isnan(d) || std::isinf(d)));
  constexpr double fmax = static_cast<double>(std::numeric_limits<float>::max());
  if (d < -fmax || d > fmax) {
    return MakeTypeErrorStatus("float", path_);
  }
  // Narrow; precision loss is allowed but overflow to inf is prevented.
  return static_cast<float>(d);
}

Result<double> JsonValue::GetDouble() const {
  const rapidjson::Value* v = VERIFY_RESULT(AsCheckedValue(impl_, path_));
  if (!v->IsNumber()) {
    return MakeTypeErrorStatus("double", path_);
  }
  double d = v->GetDouble();
  // Parse flags to accept nonstandard JSON were not set, so nan/inf is not expected.
  DCHECK(!(std::isnan(d) || std::isinf(d)));
  return d;
}

Result<std::string> JsonValue::GetString() const {
  const rapidjson::Value* v = VERIFY_RESULT(AsCheckedValue(impl_, path_));
  if (!v->IsString()) {
    return MakeTypeErrorStatus("string", path_);
  }
  return std::string(v->GetString(), v->GetStringLength());
}

Result<size_t> JsonValue::size() const {
  const rapidjson::Value* v = VERIFY_RESULT(AsCheckedValue(impl_, path_));
  if (v->IsArray()) {
    return v->Size();
  }
  if (v->IsObject()) {
    return v->MemberCount();
  }
  return MakeTypeErrorStatus("array or object", path_);
}

Result<std::string> JsonValue::ToString() const {
  const rapidjson::Value* v = VERIFY_RESULT(AsCheckedValue(impl_, path_));
  rapidjson::StringBuffer sb;
  rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
  if (!v->Accept(writer)) {
    return STATUS(RuntimeError, "Failed to serialize JSON at path: " + path_);
  }
  return std::string(sb.GetString(), sb.GetSize());
}

std::string JsonValue::Path() const {
  return path_;
}

// Helper for operator[](std::string_view key).
JsonValue JsonValue::LookupMember(const JsonValueImpl* value_impl, std::string_view key,
                                  const std::string& path) {
  const rapidjson::Value* v = AsValue(value_impl);

  const char* key_cstr;
  if (key.empty()) {
    key_cstr = "";
  } else {
    // Expect null-terminated buffer.  This works for string literals and std::string::c_str().
    DCHECK_EQ(key.data()[key.size()], '\0');
    key_cstr = key.data();
  }
  std::string member_path = MakeMemberPath(path, key_cstr);

  if (!v || !v->IsObject()) {
    return JsonValue(nullptr, member_path);
  }
  auto it = v->FindMember(key_cstr);
  if (it == v->MemberEnd()) {
    return JsonValue(nullptr, member_path);
  }
  return JsonValue(AsValueImpl(&it->value), member_path);
}

// ---------------- JsonObject implementation ----------------

JsonObject::JsonObject(const JsonValueImpl* value_impl, size_t size, const std::string& path)
  : value_impl_(DCHECK_NOTNULL(value_impl)), size_(size), path_(path) {}

JsonObject::Iterator JsonObject::begin() const {
  return Iterator(value_impl_, true /* is_begin */, path_);
}

JsonObject::Iterator JsonObject::end() const {
  return Iterator(value_impl_, false /* is_begin */, path_);
}

bool JsonObject::empty() const {
  return size_ == 0;
}

size_t JsonObject::size() const {
  return size_;
}

JsonValue JsonObject::operator[](std::string_view key) const {
  return JsonValue::LookupMember(value_impl_, key, path_);
}

// ---------------- JsonObject::Iterator implementation ----------------

struct JsonObject::Iterator::IteratorImpl {
  rapidjson::Value::ConstMemberIterator it;
  rapidjson::Value::ConstMemberIterator end;
  IteratorImpl(rapidjson::Value::ConstMemberIterator b, rapidjson::Value::ConstMemberIterator e)
    : it(b), end(e) {}
};

JsonObject::Iterator::Iterator(const JsonValueImpl* value_impl, bool is_begin,
                               const std::string& path)
  : value_impl_(DCHECK_NOTNULL(value_impl)), impl_(nullptr), path_(path) {
  const rapidjson::Value* v = AsValue(value_impl_);
  if (!v || !v->IsObject()) return;
  auto b = v->MemberBegin();
  auto e = v->MemberEnd();
  impl_ = std::make_shared<IteratorImpl>(is_begin ? b : e, e);
}

std::pair<std::string_view, JsonValue> JsonObject::Iterator::operator*() const {
  if (!impl_ || impl_->it == impl_->end) {
    return std::make_pair(std::string_view(), JsonValue(nullptr, MakeElemPath(path_, 0)));
  }
  const char* key = impl_->it->name.GetString();
  std::string_view key_view(key, impl_->it->name.GetStringLength());
  std::string member_path = MakeMemberPath(path_, key);
  return std::make_pair(key_view, JsonValue(AsValueImpl(&impl_->it->value), member_path));
}

JsonObject::Iterator& JsonObject::Iterator::operator++() {
  if (impl_ && impl_->it != impl_->end) {
    ++impl_->it;
  }
  return *this;
}

JsonObject::Iterator JsonObject::Iterator::operator++(int) {
  Iterator tmp = *this;
  ++*this;
  return tmp;
}

bool JsonObject::Iterator::operator==(const Iterator& o) const {
  if (impl_ == nullptr && o.impl_ == nullptr) {
    return true;
  }
  if (!impl_ || !o.impl_) {
    return false;
  }
  return value_impl_ == o.value_impl_ && impl_->it == o.impl_->it;
}

bool JsonObject::Iterator::operator!=(const Iterator& o) const {
  return !(*this == o);
}

// ---------------- JsonArray implementation ----------------

JsonArray::JsonArray(const JsonValueImpl* value_impl, size_t size, const std::string& path)
  : value_impl_(DCHECK_NOTNULL(value_impl)), size_(size), path_(path) {}

JsonArray::Iterator JsonArray::begin() const {
  return Iterator(value_impl_, true /* is_begin */, path_);
}

JsonArray::Iterator JsonArray::end() const {
  return Iterator(value_impl_, false /* is_begin */, path_);
}

bool JsonArray::empty() const {
  return size_ == 0;
}

size_t JsonArray::size() const {
  return size_;
}

JsonValue JsonArray::operator[](size_t index) const {
  return MakeElementFromArrayNode(value_impl_, index, path_);
}

JsonValue JsonArray::MakeElementFromArrayNode(const JsonValueImpl* value_impl, size_t index,
                                              const std::string& path) {
  const rapidjson::Value* v = AsValue(value_impl);
  std::string elem_path = MakeElemPath(path, index);
  if (!v || !v->IsArray()) {
    return JsonValue(nullptr, elem_path);
  }
  if (index >= v->Size()) {
    return JsonValue(nullptr, elem_path);
  }
  const rapidjson::Value& elem = (*v)[static_cast<rapidjson::SizeType>(index)];
  return JsonValue(AsValueImpl(&elem), elem_path);
}

// ---------------- JsonArray::Iterator implementation ----------------

struct JsonArray::Iterator::IteratorImpl {
  rapidjson::Value::ConstValueIterator it;
  rapidjson::Value::ConstValueIterator end;
  size_t idx; // for path formatting
  IteratorImpl(rapidjson::Value::ConstValueIterator b, rapidjson::Value::ConstValueIterator e,
               size_t i)
    : it(b), end(e), idx(i) {}
};

JsonArray::Iterator::Iterator(const JsonValueImpl* value_impl, bool is_begin,
                              const std::string& path)
    : value_impl_(DCHECK_NOTNULL(value_impl)), impl_(nullptr), path_(path) {
  const rapidjson::Value* v = AsValue(value_impl_);
  if (!v || !v->IsArray()) {
    return;
  }
  auto b = v->Begin();
  auto e = v->End();
  size_t start_idx = is_begin ? 0 : v->Size();
  impl_ = std::make_shared<IteratorImpl>(is_begin ? b : e, e, start_idx);
}

JsonValue JsonArray::Iterator::operator*() const {
  if (!impl_ || impl_->it == impl_->end) {
    return JsonValue(nullptr, MakeElemPath(path_, impl_ ? impl_->idx : 0));
  }
  return JsonValue(AsValueImpl(&*impl_->it), MakeElemPath(path_, impl_->idx));
}

JsonArray::Iterator& JsonArray::Iterator::operator++() {
  if (impl_ && impl_->it != impl_->end) {
    ++impl_->it;
    ++impl_->idx;
  }
  return *this;
}

JsonArray::Iterator JsonArray::Iterator::operator++(int) {
  Iterator tmp = *this;
  ++*this;
  return tmp;
}

bool JsonArray::Iterator::operator==(const Iterator& o) const {
  if (impl_ == nullptr && o.impl_ == nullptr) {
    return true;
  }
  if (!impl_ || !o.impl_) {
    return false;
  }
  return value_impl_ == o.value_impl_ && impl_->it == o.impl_->it;
}

bool JsonArray::Iterator::operator!=(const Iterator& o) const {
  return !(*this == o);
}

} // namespace yb
