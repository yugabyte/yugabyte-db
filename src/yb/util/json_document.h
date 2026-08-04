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

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <utility>

// Do not include rapidjson headers here!
//
// This file provides an interface over rapidjson using YB's Result<T> and some other tweaks.
// - JsonDocument: wrapper for rapidjson::Document
// - JsonValue: wrapper for rapidjson::Value
// - JsonObject: wrapper for rapidjson::Value::Object
// - JsonArray: wrapper for rapidjson::Value::Array

namespace yb {

// Forward-declare Status/Result to avoid including their headers here.
// The .cc will include the actual headers.
class Status;
template<class TValue> class Result;

class JsonValue; // forward

// Opaque JsonValueImpl type (defined in .cc). Declared here as incomplete to avoid pulling in
// rapidjson headers into the public header.
struct JsonValueImpl;

// JsonDocument: owns parsed document and provides Root() for chainable reads.
class JsonDocument {
 public:
  JsonDocument();
  ~JsonDocument();

  JsonDocument(JsonDocument&&) noexcept;
  JsonDocument& operator=(JsonDocument&&) noexcept;
  JsonDocument(const JsonDocument&) = delete;
  JsonDocument& operator=(const JsonDocument&) = delete;

  // Parse JSON text.  On failure, returns...
  // - InvalidArgument on parse error.
  Result<JsonValue> Parse(const char* json);
  Result<JsonValue> Parse(std::string_view sv);

  // Root accessor for chainable reads. Always returns a JsonValue (may be invalid).
  JsonValue Root();

 private:
  struct Impl;

  std::unique_ptr<Impl> impl_;
};

// Iterable non-owning JSON object view.
// Iteration yields std::pair<std::string, JsonValue> (key, value). key is guaranteed
// null-terminated by rapidjson.
class JsonObject {
 public:
  class Iterator {
   public:
    using value_type = std::pair<std::string_view, JsonValue>;
    using difference_type = std::ptrdiff_t;
    using pointer = void;
    using reference = JsonValue;
    using iterator_category = std::forward_iterator_tag;

    Iterator(const JsonValueImpl* value_impl, bool is_begin, const std::string& path);

    std::pair<std::string_view, JsonValue> operator*() const;
    Iterator& operator++();           // ++it
    Iterator operator++(int);         // it++
    bool operator==(const Iterator& o) const;
    bool operator!=(const Iterator& o) const;

   private:
    struct IteratorImpl;

    const JsonValueImpl* value_impl_;
    std::shared_ptr<IteratorImpl> impl_;
    const std::string path_;
  };

  JsonObject(const JsonValueImpl* value_impl, size_t size, const std::string& path);

  Iterator begin() const;
  Iterator end() const;
  bool empty() const;
  size_t size() const;
  JsonValue operator[](std::string_view key) const;

 private:
  const JsonValueImpl* value_impl_;
  const size_t size_;
  const std::string path_;
};

// Iterable non-owning JSON array view.
// Iteration yields JsonValue.
class JsonArray {
 public:
  class Iterator {
   public:
    using value_type = JsonValue;
    using difference_type = std::ptrdiff_t;
    using pointer = void;
    using reference = JsonValue;
    using iterator_category = std::forward_iterator_tag;

    Iterator(const JsonValueImpl* value_impl, bool is_begin, const std::string& path);

    JsonValue operator*() const;
    Iterator& operator++();           // ++it
    Iterator operator++(int);         // it++
    bool operator==(const Iterator& o) const;
    bool operator!=(const Iterator& o) const;

   private:
    struct IteratorImpl;
    const JsonValueImpl* value_impl_;
    std::shared_ptr<IteratorImpl> impl_;
    const std::string path_;
  };

  JsonArray(const JsonValueImpl* value_impl, size_t size, const std::string& path);

  Iterator begin() const;
  Iterator end() const;
  bool empty() const;
  size_t size() const;
  JsonValue operator[](size_t index) const;

 private:
  static JsonValue MakeElementFromArrayNode(const JsonValueImpl* value_impl, size_t index,
                                            const std::string& path);

  const JsonValueImpl* value_impl_;
  const size_t size_;
  const std::string path_;
};

// JsonValue: chainable read-only JSON view. Copyable and lightweight.
// Getters return Result<T>. operator[] preserves chaining; errors are returned by getters.
class JsonValue {
 public:
  JsonValue();
  ~JsonValue();

  // Chainable indexed/member access. Always safe: returns an invalid JsonValue if access fails.
  JsonValue operator[](size_t index) const;
  JsonValue operator[](std::string_view key) const;

  // Whether this JsonValue's path exists.
  constexpr bool IsValid() const noexcept { return impl_ != nullptr; }

  // Type queries.
  bool IsNull() const noexcept;
  bool IsBool() const noexcept;
  bool IsObject() const noexcept;
  bool IsArray() const noexcept;
  bool IsNumber() const noexcept;
  bool IsInt32() const noexcept;
  bool IsUint32() const noexcept;
  bool IsInt64() const noexcept;
  bool IsUint64() const noexcept;
  bool IsFloat() const noexcept;
  bool IsDouble() const noexcept;
  bool IsString() const noexcept;

  // Safe getters.  On failure, returns...
  // - NotFound: path does not exist.
  // - InvalidArgument: wrong type.
  Result<bool> GetBool() const;
  Result<JsonObject> GetObject() const;
  Result<JsonArray> GetArray() const;
  Result<int32_t> GetInt32() const;
  Result<uint32_t> GetUint32() const;
  Result<int64_t> GetInt64() const;
  Result<uint64_t> GetUint64() const;
  Result<float> GetFloat() const;
  Result<double> GetDouble() const;
  Result<std::string> GetString() const;

  // Get size of Object or Array.  On failure, returns...
  // - NotFound: path does not exist.
  // - InvalidArgument: wrong type.
  Result<size_t> size() const;

  // Compact textual representation of the JSON.  On failure, returns...
  // - NotFound: path does not exist.
  Result<std::string> ToString() const;
  // Return textual path from root (e.g., ".[0].foo.bar").  For performance, does not escape special
  // characters such as dots and brackets.
  std::string Path() const;

 private:
  friend class JsonArray;
  friend class JsonObject;
  friend class JsonDocument;

  // Construct from internal impl pointer (defined in .cc).
  JsonValue(const JsonValueImpl* impl, std::string path);

  static JsonValue LookupMember(const JsonValueImpl* impl, std::string_view key,
                                const std::string& path);

  const JsonValueImpl* impl_;     // opaque pointer to const rapidjson::Value
  std::string path_;              // human-readable path, efficient for typical short paths
};

} // namespace yb
