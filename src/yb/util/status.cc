// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
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

// Portions Copyright (c) YugaByte, Inc.
#include "yb/util/status.h"

#include <array>
#include <atomic>
#include <regex>
#include <string_view>

#include "yb/gutil/dynamic_annotations.h"
#include "yb/util/debug-util.h"
#include "yb/util/malloc.h"
#include "yb/util/slice.h"
#include "yb/util/status_ec.h"

namespace yb {

namespace {

#ifndef NDEBUG
// This allows to dump stack traces whenever an error status matching a certain regex is generated.
boost::optional<std::regex> StatusStackTraceRegEx() {
  const char* regex_str = getenv("YB_STACK_TRACE_ON_ERROR_STATUS_RE");
  if (!regex_str) {
    return boost::none;
  }
  return std::regex(regex_str);
}
#endif

namespace {

struct StatusCategories {
  // Category is stored as uint8_t, so there could be only 256 categories.
  std::array<StatusCategoryDescription, 0x100> categories;

  std::string KnownCategoriesStr() {
    std::ostringstream ss;
    bool first = true;
    for (size_t i = 0; i < categories.size(); ++i) {
      const auto& category = categories[i];
      if (category.name != nullptr) {
        if (!first) {
          ss << ", ";
        }
        first = false;
        ss << i << "=" << *category.name;
      }
    }
    return ss.str();
  }

  // In debug mode log as many details as possible and crash.
  // In release mode log a warning.
  void ReportMissingCategory(uint8_t category_id, const char* function_name) {
#ifndef NDEBUG
    LOG(WARNING) << "Known categories: " << KnownCategoriesStr();
#endif
    LOG(DFATAL) << "In " << function_name
                << ": unknown category description for " << static_cast<int>(category_id);
  }

  void Register(const StatusCategoryDescription& description) {
    CHECK(!categories[description.id].name);
    categories[description.id] = description;
  }

  const std::string& CategoryName(uint8_t category) {
    const auto& description = categories[category];
    if (!description.name) {
      static std::string kUnknownCategory("unknown error category");
      return kUnknownCategory;
    }
    return *description.name;
  }

  // Returns data slice w/o category for error code starting with start.
  Slice GetSlice(const uint8_t* start) {
    const uint8_t category_id = *start;
    const auto& description = categories[category_id];
    if (description.name == nullptr) {
      if (category_id > 0)
        ReportMissingCategory(category_id, __func__);
      return Slice();
    }
    return Slice(start, 1 + description.decode_size(start + 1));
  }

  std::string ToString(Slice encoded_err_code) {
    const uint8_t category_id = *encoded_err_code.data();
    const auto& description = categories[category_id];
    if (description.name == nullptr) {
      if (category_id > 0)
        ReportMissingCategory(category_id, __func__);
      return std::string();
    }
    return description.to_string(encoded_err_code.data() + 1);
  }
};

StatusCategories& status_categories() {
  static StatusCategories result;
  return result;
}

} // namespace

class ErrorCodeIterator : public std::iterator<std::forward_iterator_tag, Slice> {
 public:
  explicit ErrorCodeIterator(const char* address)
      : current_(address ? status_categories().GetSlice(pointer_cast<const uint8_t*>(address))
                         : Slice()) {}

  bool is_end() const {
    return current_.empty();
  }

  value_type operator*() const {
    return current_;
  }

  ErrorCodeIterator& operator++() {
    current_ = status_categories().GetSlice(current_.end());
    return *this;
  }

 private:
  friend bool operator==(const ErrorCodeIterator& lhs, const ErrorCodeIterator& rhs) {
    return lhs.is_end() ? rhs.is_end() : lhs.current_.data() == rhs.current_.data();
  }

  friend bool operator!=(const ErrorCodeIterator& lhs, const ErrorCodeIterator& rhs) {
    return !(lhs == rhs);
  }

  Slice current_;
};

uint8_t* DoEncode(const StatusErrorCode& error, uint8_t* out) {
  *out++ = error.Category();
  auto new_out = error.Encode(out);
  DCHECK_EQ(out + error.EncodedSize(), new_out);
  return new_out;
}

class ErrorCodesRange {
 public:
  explicit ErrorCodesRange(const char* start) : start_(start) {
  }

  typedef ErrorCodeIterator const_iterator;

  const_iterator begin() const {
    return ErrorCodeIterator(start_);
  }

  const_iterator end() const {
    return ErrorCodeIterator(nullptr);
  }

 private:
  const char* start_;
};

template<class SizeType>
uint8_t* StoreString(const std::string& str, uint8_t* out) {
  Store<SizeType, LittleEndian>(out, str.size());
  out += sizeof(SizeType);
  memcpy(out, str.data(), str.size());
  out += str.size();
  return out;
}

template<class SizeType>
size_t StringEncodedSize(const std::string& str) {
  return sizeof(SizeType) + str.size();
}

template<class SizeType>
std::string_view DecodeString(Slice* source) {
  const auto str_size = Load<SizeType, LittleEndian>(source->cdata());
  source->remove_prefix(sizeof(SizeType));
  const std::string_view result(source->cdata(), str_size);
  source->remove_prefix(str_size);
  return result;
}

} // anonymous namespace

StringBackedErrorTag::Value StringBackedErrorTag::Decode(const uint8_t* source) {
  if (!source) {
    return Value();
  }
  Slice buf(source, DecodeSize(source));
  return StringBackedErrorTag::Value(DecodeString<SizeType>(&buf));
}

size_t StringBackedErrorTag::EncodedSize(const StringBackedErrorTag::Value& value) {
  return StringEncodedSize<SizeType>(value);
}

uint8_t* StringBackedErrorTag::Encode(const StringBackedErrorTag::Value& value, uint8_t* out) {
  return StoreString<SizeType>(value, out);
}

StringVectorBackedErrorTag::Value StringVectorBackedErrorTag::Decode(const uint8_t* source) {
  if (!source) {
    return Value();
  }
  Value result;
  Slice buf(source, DecodeSize(source));
  buf.remove_prefix(sizeof(SizeType));
  while (buf.size() > 0) {
    result.emplace_back(DecodeString<SizeType>(&buf));
  }
  return result;
}

size_t StringVectorBackedErrorTag::EncodedSize(const StringVectorBackedErrorTag::Value& value) {
  size_t size = sizeof(SizeType);
  for (const auto& str : value) {
    size += StringEncodedSize<SizeType>(str);
  }
  return size;
}

uint8_t* StringVectorBackedErrorTag::Encode(
    const StringVectorBackedErrorTag::Value& value, uint8_t* out) {
  uint8_t* const start = out;
  out += sizeof(SizeType);
  for (const auto& str : value) {
    out = StoreString<SizeType>(str, out);
  }
  Store<SizeType, LittleEndian>(start, out - start);
  return out;
}

// Error codes are stored after message.
// For each error code first byte encodes category and a special value of 0 means the end of the
// list. Error code is encoded after category and concrete encoding depends on the category.
struct Status::State {
  State(const State&) = delete;
  void operator=(const State&) = delete;

  std::atomic<size_t> counter;
  uint32_t message_len;
  uint8_t code;
  // This must always be a pointer to a constant string.
  // The status object does not own this string.
  const char* file_name;
  int line_number;
  char message[1];

  template <class Errors>
  static StatePtr Create(
      Code code, const char* file_name, int line_number, const Slice& msg, const Slice& msg2,
      const Errors& errors, size_t file_name_len);

  ErrorCodesRange error_codes() const {
    return ErrorCodesRange(message + message_len);
  }

  const char* ErrorCodesEnd() const {
    Slice last(message + message_len, size_t(0));
    for (Slice current : error_codes()) {
      last = current;
    }
    return last.cend() + 1;
  }

  Slice ErrorCodesSlice() const {
    return Slice(message + message_len, ErrorCodesEnd());
  }

  bool FileNameDuplicated() const {
    return file_name == ErrorCodesEnd();
  }

  static size_t ErrorsSize(const StatusErrorCode* error) {
    size_t result = 1;
    if (error) {
      result += 1 + error->EncodedSize();
    }
    return result;
  }

  static uint8_t* StoreErrors(const StatusErrorCode* error, uint8_t* out) {
    if (error) {
      out = DoEncode(*error, out);
    }
    *out++ = 0;
    return out;
  }

  static size_t ErrorsSize(const Slice& errors) {
    return errors.size();
  }

  static uint8_t* StoreErrors(const Slice& errors, uint8_t* out) {
    memcpy(out, errors.data(), errors.size());
    return out + errors.size();
  }
};

template <class Errors>
Status::StatePtr Status::State::Create(
    Code code, const char* file_name, int line_number, const Slice& msg, const Slice& msg2,
    const Errors& errors, size_t file_name_len) {
  static constexpr size_t kHeaderSize = offsetof(State, message);

  assert(code != kOk);
  const size_t len1 = msg.size();
  const size_t len2 = msg2.size();
  const size_t size = len1 + (len2 ? (2 + len2) : 0);
  const size_t errors_size = ErrorsSize(errors);
  size_t file_name_size = 0;
  if (file_name_len) {
    file_name_size = file_name_len + 1;
  }
  StatePtr result(static_cast<State*>(malloc(size + kHeaderSize + errors_size + file_name_size)));
  result->message_len = static_cast<uint32_t>(size);
  result->code = static_cast<uint8_t>(code);
  // We aleady assigned intrusive_ptr, so counter should be one.
  result->counter.store(1, std::memory_order_relaxed);
  result->line_number = line_number;
  memcpy(result->message, msg.data(), len1);
  if (len2) {
    result->message[len1] = ':';
    result->message[len1 + 1] = ' ';
    memcpy(result->message + 2 + len1, msg2.data(), len2);
  }
  auto errors_start = pointer_cast<uint8_t*>(&result->message[0] + size);
  auto out = StoreErrors(errors, errors_start);
  DCHECK_EQ(out, errors_start + errors_size);
  if (file_name_len) {
    auto new_file_name = out;
    memcpy(new_file_name, file_name, file_name_len);
    new_file_name[file_name_len] = 0;
    file_name = pointer_cast<char*>(new_file_name);
  }

  result->file_name = file_name;

  return result;
}

Status::Status(Code code,
               const char* file_name,
               int line_number,
               const Slice& msg,
               const Slice& msg2,
               const StatusErrorCode* error,
               size_t file_name_len)
    : state_(State::Create(code, file_name, line_number, msg, msg2, error, file_name_len)) {
#ifndef NDEBUG
  static const bool print_stack_trace = getenv("YB_STACK_TRACE_ON_ERROR_STATUS") != nullptr;
  static const boost::optional<std::regex> status_stack_trace_re =
      StatusStackTraceRegEx();

  std::string string_rep;  // To avoid calling ToString() twice.
  if (print_stack_trace ||
      (status_stack_trace_re &&
       std::regex_search(string_rep = ToString(), *status_stack_trace_re))) {
    if (string_rep.empty()) {
      string_rep = ToString();
    }
    // We skip a couple of top frames like these:
    //    ~/code/yugabyte/src/yb/util/status.cc:53:
    //        @ yb::Status::Status(yb::Status::Code, yb::Slice const&, yb::Slice const&, long,
    //                             char const*, int)
    //    ~/code/yugabyte/src/yb/util/status.h:137:
    //        @ yb::STATUS(Corruption, char const*, int, yb::Slice const&, yb::Slice const&, short)
    //    ~/code/yugabyte/src/yb/common/doc_hybrid_time.cc:94:
    //        @ yb::DocHybridTime::DecodeFrom(yb::Slice*)
    LOG(WARNING) << "Non-OK status generated: " << string_rep << ", stack trace:\n"
                 << GetStackTrace(StackTraceLineFormat::DEFAULT, /* skip frames: */ 1);
  }
#endif
}

Status::Status(Code code,
               const char* file_name,
               int line_number,
               const Slice& msg,
               const Slice& error,
               size_t file_name_len)
    : state_(State::Create(code, file_name, line_number, msg, Slice(), error, file_name_len)) {
}

Status::Status(StatePtr state)
    : state_(std::move(state)) {
}

Status::Status(YbcStatusStruct* state, AddRef add_ref)
    : state_(pointer_cast<State*>(state), add_ref) {
}

Status::Status(Code code,
               const char* file_name,
               int line_number,
               const StatusErrorCode& error,
               size_t file_name_len)
    : Status(code, file_name, line_number, error.Message(), Slice(), error, file_name_len) {
}

Status::Status(Code code,
       const char* file_name,
       int line_number,
       const Slice& msg,
       const StatusErrorCode& error,
       size_t file_name_len)
    : Status(code, file_name, line_number, msg, error.Message(), error, file_name_len) {
}

YbcStatusStruct* Status::RetainStruct() const {
  if (state_) {
    intrusive_ptr_add_ref(state_.get());
  }
  return pointer_cast<YbcStatusStruct*>(state_.get());
}

YbcStatusStruct* Status::DetachStruct() {
  return pointer_cast<YbcStatusStruct*>(state_.detach());
}

const char* Status::CodeAsCString() const {
  switch (code()) {
  #define YB_STATUS_CODE(name, pb_name, value, message) \
    case Status::BOOST_PP_CAT(k, name): \
      return message;
  #include "yb/util/status_codes.h"
  #undef YB_STATUS_CODE
  }
  return nullptr;
}

std::string Status::CodeAsString() const {
  auto* cstr = CodeAsCString();
  return cstr != nullptr ? cstr : "Incorrect status code " + std::to_string(code());
}

std::string Status::ToString(bool include_file_and_line, bool include_code) const {
  std::string result;

  if (include_code) {
    result += CodeAsString();
  }

  if (state_ == nullptr) {
    return result;
  }

  if (include_file_and_line && state_->file_name != nullptr && state_->line_number) {
    result.append(result.empty() ? "(" : " (");

    // Try to only include file path starting from source root directory. We are assuming that all
    // C++ code is located in $YB_SRC_ROOT/src, where $YB_SRC_ROOT is the repository root. Note that
    // this will break if the repository itself is located in a parent directory named "src".
    // However, neither Jenkins, nor our standard code location on a developer workstation
    // (~/code/yugabyte) should have that problem.
    const char* src_subpath = strstr(state_->file_name, "/src/");
    result.append(src_subpath != nullptr ? src_subpath + 5 : state_->file_name);

    result.append(":");
    result.append(std::to_string(state_->line_number));
    result.append(")");
  }

  if (!result.empty()) {
    result.append(": ");
  }

  Slice msg = message();
  result.append(reinterpret_cast<const char*>(msg.data()), msg.size());

  // If no message (rare case) - show code (if it's not shown yet).
  if (result.empty()) {
    result += CodeAsString();
  }

  for (auto slice : state_->error_codes()) {
    result += " (";
    result += CategoryName(*slice.data());
    result += ' ';
    result += status_categories().ToString(slice);
    result += ')';
  }

  return result;
}

Slice Status::message() const {
  if (state_ == nullptr) {
    return Slice();
  }

  return Slice(state_->message, state_->message_len);
}

const char* Status::file_name() const {
  return state_ ? state_->file_name : "";
}

int Status::line_number() const {
  return state_ ? state_->line_number : 0;
}

Status::Code Status::code() const {
  return !state_ ? kOk : static_cast<Code>(state_->code);
}

Status Status::CloneAndPrepend(const Slice& msg) const {
  return Status(State::Create(
      code(), state_->file_name, state_->line_number, msg, message(), state_->ErrorCodesSlice(),
      file_name_len_for_copy()));
}

Status Status::CloneAndReplaceCode(Code code) const {
  return Status(State::Create(
      code, state_->file_name, state_->line_number, message(), Slice(), state_->ErrorCodesSlice(),
      file_name_len_for_copy()));
}

Status Status::CloneAndAppend(const Slice& msg) const {
  return Status(State::Create(
      code(), state_->file_name, state_->line_number, message(), msg, state_->ErrorCodesSlice(),
      file_name_len_for_copy()));
}

Status Status::CloneAndAddErrorCode(const StatusErrorCode& error_code) const {
  auto errors_slice = state_->ErrorCodesSlice();
  size_t new_errors_size = errors_slice.size() + 1 + error_code.EncodedSize();
  auto buffer = static_cast<uint8_t*>(alloca(new_errors_size));
  auto out = buffer;
  bool inserted = false;
  // Insert encoded error code to existing list of error codes.
  // Which is ordered by category.
  for (const auto error : state_->error_codes()) {
    auto current_category = *error.data();
    // Appropriate place to insert new error code, when existing category is greater
    // and we did not insert yet.
    if (!inserted && current_category >= error_code.Category()) {
      size_t size = error.data() - errors_slice.data();
      memcpy(out, errors_slice.data(), size);
      out += size;
      out = DoEncode(error_code, out);
      // Copy remaining errors.
      if (current_category != error_code.Category()) {
        size_t size = errors_slice.end() - error.data();
        memcpy(out, error.data(), size);
        out += size;
      } else {
        // Skip error with the same code.
        size_t size = errors_slice.end() - error.end();
        memcpy(out, error.end(), size);
        out += size;
        new_errors_size -= error.size();
      }
      inserted = true;
      break;
    }
  }
  // There is no error code with category greater than added, so add to end of the list.
  if (!inserted) {
    // Don't copy terminating zero.
    memcpy(out, errors_slice.data(), errors_slice.size() - 1);
    out += errors_slice.size() - 1;
    out = DoEncode(error_code, out);
    *out++ = 0;
  }
  size_t encoded_size = out - buffer;
  LOG_IF(DFATAL, encoded_size != new_errors_size)
      << "New error codes size is expected to be " << new_errors_size << " but " << encoded_size
      << " bytes were encoded";
  return Status(State::Create(
      code(), state_->file_name, state_->line_number, message(), Slice(),
      Slice(buffer, new_errors_size), file_name_len_for_copy()));
}

size_t Status::memory_footprint_excluding_this() const {
  return state_ ? malloc_usable_size(state_.get()) : 0;
}

size_t Status::memory_footprint_including_this() const {
  return malloc_usable_size(this) + memory_footprint_excluding_this();
}

bool Status::file_name_duplicated() const {
  return state_->FileNameDuplicated();
}

size_t Status::file_name_len_for_copy() const {
  return file_name_duplicated() ? strlen(state_->file_name) : 0;
}

const uint8_t* Status::ErrorData(uint8_t category) const {
  if (!state_) {
    return nullptr;
  }

  for (auto slice : state_->error_codes()) {
    if (*slice.data() == category) {
      return slice.data() + 1;
    }
  }

  return nullptr;
}

Slice Status::ErrorCodesSlice() const {
  if (!state_) {
    return Slice();
  }

  return state_->ErrorCodesSlice();
}

void intrusive_ptr_release(Status::State* state) {
  if (state->counter.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    free(state);
  }
}

void intrusive_ptr_add_ref(Status::State* state) {
  state->counter.fetch_add(1, std::memory_order_relaxed);
}

void Status::RegisterCategory(const StatusCategoryDescription& description) {
  status_categories().Register(description);
}

const std::string& Status::CategoryName(uint8_t category) {
  return status_categories().CategoryName(category);
}

StatusCategoryRegisterer::StatusCategoryRegisterer(const StatusCategoryDescription& description) {
  Status::RegisterCategory(description);
}

std::string StringVectorBackedErrorTag::DecodeToString(const uint8_t* source) {
  return AsString(Decode(source));
}

void StatusCheck(bool value) {
  CHECK(value);
}

Status StatusHolder::GetStatus() const {
  if (PREDICT_TRUE(is_ok_.load(std::memory_order_acquire))) {
    return Status::OK();
  }
  std::lock_guard lock(mutex_);
  return status_;
}

void StatusHolder::SetError(const Status& status) {
  std::lock_guard lock(mutex_);
  status_ = status;
  is_ok_.store(false, std::memory_order_release);
}

void StatusHolder::Reset() {
  std::lock_guard lock(mutex_);
  status_ = Status::OK();
  is_ok_.store(true, std::memory_order_release);
}


}  // namespace yb
