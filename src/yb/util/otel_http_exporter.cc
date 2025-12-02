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

#include "yb/util/otel_http_exporter.h"

#include <cstdlib>
#include <iomanip>
#include <sstream>

#include "yb/util/curl_util.h"
#include "yb/util/faststring.h"
#include "yb/util/logging.h"
#include "yb/util/status.h"

namespace yb {

namespace {

// Global singleton instance
SimpleOtlpHttpSender g_otlp_sender;

}  // namespace

SimpleOtlpHttpSender::SimpleOtlpHttpSender()
    : enabled_(false) {
  const char* service_name_env = std::getenv("OTEL_SERVICE_NAME");
  service_name_ = (service_name_env && service_name_env[0] != '\0') ? service_name_env : "yugabyte";
}

SimpleOtlpHttpSender::~SimpleOtlpHttpSender() = default;

void SimpleOtlpHttpSender::SetEndpoint(const std::string& endpoint) {
  endpoint_ = endpoint;
  enabled_.store(!endpoint_.empty(), std::memory_order_release);
}

void SimpleOtlpHttpSender::SetServiceName(const std::string& service_name) {
  service_name_ = service_name;
}

bool SimpleOtlpHttpSender::IsEnabled() const {
  return enabled_.load(std::memory_order_acquire);
}

bool SimpleOtlpHttpSender::SendSpan(const SimpleSpanData& span) {
  std::vector<SimpleSpanData> spans;
  spans.push_back(span);
  return SendSpans(spans);
}

bool SimpleOtlpHttpSender::SendSpans(const std::vector<SimpleSpanData>& spans) {
  if (!IsEnabled() || spans.empty()) {
    return true;
  }

  std::string json_payload = SpansToJson(spans);

  LOG(INFO) << "[OTEL] Sending " << spans.size() << " span(s) to " << endpoint_;
  VLOG(3) << "[OTEL] Payload: " << json_payload;

  try {
    EasyCurl curl;
    faststring response;

    auto status = curl.PostToURL(
        endpoint_,
        json_payload,
        "application/json",
        &response,
        5  // 5 second timeout
    );

    if (!status.ok()) {
      LOG(WARNING) << "[OTEL] HTTP POST to " << endpoint_ << " failed: " << status.ToString();
      return false;
    }

    LOG(INFO) << "[OTEL] Successfully sent " << spans.size() << " span(s), response: " 
              << response.size() << " bytes";
    return true;

  } catch (const std::exception& e) {
    LOG(ERROR) << "[OTEL] Exception sending to collector: " << e.what();
    return false;
  }
}

std::string SimpleOtlpHttpSender::JsonEscape(const std::string& str) {
  std::ostringstream oss;
  for (size_t i = 0; i < str.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(str[i]);
    switch (c) {
      case '"':  oss << "\\\""; break;
      case '\\': oss << "\\\\"; break;
      case '\b': oss << "\\b"; break;
      case '\f': oss << "\\f"; break;
      case '\n': oss << "\\n"; break;
      case '\r': oss << "\\r"; break;
      case '\t': oss << "\\t"; break;
      default:
        if (c < 0x20) {
          oss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
        } else {
          oss << c;
        }
        break;
    }
  }
  return oss.str();
}

std::string SimpleOtlpHttpSender::SpansToJson(const std::vector<SimpleSpanData>& spans) const {
  std::ostringstream json;

  json << R"({"resourceSpans":[{"resource":{"attributes":[)";
  json << R"({"key":"service.name","value":{"stringValue":")" << JsonEscape(service_name_) << R"("}},)";
  json << R"({"key":"telemetry.sdk.language","value":{"stringValue":"cpp"}},)";
  json << R"({"key":"telemetry.sdk.name","value":{"stringValue":"yugabyte-simple"}})";
  json << R"(]},"scopeSpans":[{"scope":{"name":"yugabyte","version":"1.0.0"},"spans":[)";

  bool first_span = true;
  for (size_t i = 0; i < spans.size(); ++i) {
    const SimpleSpanData& span = spans[i];

    if (!first_span) {
      json << ",";
    }
    first_span = false;

    json << "{";
    json << R"("traceId":")" << span.trace_id << R"(",)";
    json << R"("spanId":")" << span.span_id << R"(",)";

    if (!span.parent_span_id.empty()) {
      json << R"("parentSpanId":")" << span.parent_span_id << R"(",)";
    }

    json << R"("name":")" << JsonEscape(span.name) << R"(",)";
    json << R"("kind":)" << span.kind << ",";
    json << R"("startTimeUnixNano":")" << span.start_time_ns << R"(",)";
    json << R"("endTimeUnixNano":")" << span.end_time_ns << R"(",)";

    json << R"("attributes":[)";
    bool first_attr = true;
    for (size_t j = 0; j < span.string_attributes.size(); ++j) {
      if (!first_attr) json << ",";
      first_attr = false;
      json << R"({"key":")" << JsonEscape(span.string_attributes[j].first)
           << R"(","value":{"stringValue":")" << JsonEscape(span.string_attributes[j].second) << R"("}})";
    }
    for (size_t j = 0; j < span.int_attributes.size(); ++j) {
      if (!first_attr) json << ",";
      first_attr = false;
      json << R"({"key":")" << JsonEscape(span.int_attributes[j].first)
           << R"(","value":{"intValue":")" << span.int_attributes[j].second << R"("}})";
    }
    json << "],";

    json << R"("status":{"code":)" << span.status_code;
    if (!span.status_message.empty()) {
      json << R"(,"message":")" << JsonEscape(span.status_message) << R"(")";
    }
    json << "}}";
  }

  json << "]}]}]}";
  return json.str();
}

SimpleOtlpHttpSender& GetGlobalOtlpSender() {
  return g_otlp_sender;
}

void InitGlobalOtlpSenderFromEnv(const std::string& service_name) {
  std::string endpoint;
  const char* endpoint_env = std::getenv("OTEL_EXPORTER_OTLP_ENDPOINT");

  if (endpoint_env && endpoint_env[0] != '\0') {
    endpoint = endpoint_env;
    if (endpoint.find("/v1/traces") == std::string::npos) {
      if (!endpoint.empty() && endpoint.back() == '/') {
        endpoint.pop_back();
      }
      endpoint += "/v1/traces";
    }
  } else {
    endpoint = "http://localhost:4318/v1/traces";
  }

  g_otlp_sender.SetEndpoint(endpoint);
  LOG(INFO) << "[OTEL] HTTP exporter endpoint: " << endpoint;

  const char* service_name_env = std::getenv("OTEL_SERVICE_NAME");
  std::string resolved_service_name = (service_name_env && service_name_env[0] != '\0')
      ? service_name_env : "yugabyte";
  g_otlp_sender.SetServiceName(resolved_service_name);
  LOG(INFO) << "[OTEL] HTTP exporter service name: " << resolved_service_name;
}

}  // namespace yb
