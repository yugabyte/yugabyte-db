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

#include <atomic>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

namespace yb {

// Simple span data structure - no OTEL SDK dependencies.
// This allows us to capture span data without pulling in any OTEL headers.
struct SimpleSpanData {
  std::string trace_id;        // 32-char hex string
  std::string span_id;         // 16-char hex string
  std::string parent_span_id;  // 16-char hex string or empty
  std::string name;
  int64_t start_time_ns;       // Nanoseconds since Unix epoch
  int64_t end_time_ns;         // Nanoseconds since Unix epoch
  int kind;                    // 1=internal, 2=server, 3=client, 4=producer, 5=consumer
  int status_code;             // 0=unset, 1=ok, 2=error
  std::string status_message;
  std::vector<std::pair<std::string, std::string> > string_attributes;
  std::vector<std::pair<std::string, int64_t> > int_attributes;

  SimpleSpanData()
      : start_time_ns(0), end_time_ns(0), kind(1), status_code(0) {}
};

// Minimal OTLP/HTTP JSON sender for OpenTelemetry traces.
// This class has ZERO OTEL SDK dependencies - it works entirely with SimpleSpanData
// and standard C++ types, avoiding any potential ABI/template issues.
//
// Usage:
//   SimpleOtlpHttpSender sender;
//   sender.SetServiceName("my-service");
//   sender.SetEndpoint("http://localhost:4318/v1/traces");
//
//   SimpleSpanData span;
//   span.trace_id = "...";
//   // ... fill in other fields
//   sender.SendSpan(span);
//
class SimpleOtlpHttpSender {
 public:
  SimpleOtlpHttpSender();
  ~SimpleOtlpHttpSender();

  // Configuration
  void SetEndpoint(const std::string& endpoint);
  void SetServiceName(const std::string& service_name);

  // Send a single span to the collector. Returns true on success.
  bool SendSpan(const SimpleSpanData& span);

  // Send multiple spans in a single request. Returns true on success.
  bool SendSpans(const std::vector<SimpleSpanData>& spans);

  // Check if the sender is enabled (endpoint is set)
  bool IsEnabled() const;

 private:
  // Convert spans to OTLP/HTTP JSON format
  std::string SpansToJson(const std::vector<SimpleSpanData>& spans) const;

  // Escape a string for JSON
  static std::string JsonEscape(const std::string& str);

  std::string endpoint_;
  std::string service_name_;
  std::atomic<bool> enabled_;
};

// Global singleton accessor for convenience
SimpleOtlpHttpSender& GetGlobalOtlpSender();

// Initialize the global sender from environment variables:
// - OTEL_EXPORTER_OTLP_ENDPOINT: Base endpoint (default: http://localhost:4318)
// The service_name parameter should be the already-resolved service name.
void InitGlobalOtlpSenderFromEnv(const std::string& service_name);

}  // namespace yb
