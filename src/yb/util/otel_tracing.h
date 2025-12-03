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

#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "yb/util/status.h"

namespace yb {

namespace otel_internal {
struct SpanContext;
}  // namespace otel_internal

class OtelSpanHandle {
 public:
  OtelSpanHandle();
  ~OtelSpanHandle();

  // Move-only type
  OtelSpanHandle(OtelSpanHandle&& other) noexcept;
  OtelSpanHandle& operator=(OtelSpanHandle&& other) noexcept;

  OtelSpanHandle(const OtelSpanHandle&) = delete;
  OtelSpanHandle& operator=(const OtelSpanHandle&) = delete;

  void SetStatus(bool ok, const std::string& description = "");

  void SetAttribute(const std::string& key, const std::string& value);
  void SetAttribute(const std::string& key, int64_t value);
  void SetAttribute(const std::string& key, double value);
  void SetAttribute(const std::string& key, bool value);

  bool IsActive() const;

 private:
  friend class OtelTracing;
  explicit OtelSpanHandle(std::unique_ptr<otel_internal::SpanContext> context);
  
  std::unique_ptr<otel_internal::SpanContext> context_;
};

class OtelTracing {
 public:
  static Status InitFromEnv(const std::string& default_service_name);
  static void Shutdown();
  static bool IsEnabled();
  static OtelSpanHandle StartSpan(const std::string& name);
  static OtelSpanHandle StartSpanFromTraceparent(
      const std::string& name,
      const std::string& traceparent);
  static void AdoptSpan(const OtelSpanHandle& span);
  static void EndCurrentSpan();
  static bool HasActiveContext();
  static std::string GetCurrentTraceparent();

 private:
  OtelTracing() = delete;
};

class ScopedOtelSpan {
 public:
  explicit ScopedOtelSpan(OtelSpanHandle&& span)
      : span_(std::move(span)) {
    if (span_.IsActive()) {
      OtelTracing::AdoptSpan(span_);
    }
  }

  ~ScopedOtelSpan() {
    if (span_.IsActive()) {
      OtelTracing::EndCurrentSpan();
    }
  }

  ScopedOtelSpan(const ScopedOtelSpan&) = delete;
  ScopedOtelSpan& operator=(const ScopedOtelSpan&) = delete;

  OtelSpanHandle& span() { return span_; }
  const OtelSpanHandle& span() const { return span_; }

 private:
  OtelSpanHandle span_;
};

}  // namespace yb

