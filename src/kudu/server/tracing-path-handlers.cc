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
#include "kudu/server/tracing-path-handlers.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <boost/bind.hpp>
#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/rapidjson.h>
#include <rapidjson/stringbuffer.h>

#include "kudu/gutil/strings/escaping.h"
#include "kudu/util/jsonwriter.h"
#include "kudu/util/debug/trace_event_impl.h"

using std::map;
using std::string;
using std::stringstream;
using std::vector;

using kudu::debug::CategoryFilter;
using kudu::debug::TraceLog;
using kudu::debug::TraceResultBuffer;

namespace kudu {
namespace server {

enum Handler {
  kBeginMonitoring,
  kEndMonitoring,
  kCaptureMonitoring,
  kGetMonitoringStatus,
  kCategories,
  kBeginRecording,
  kGetBufferPercentFull,
  kEndRecording,
  kSimpleDump
};

namespace {

Status ParseBase64JsonRequest(const string& json_base64,
                              rapidjson::Document* doc) {
  string json_str;
  if (!Base64Unescape(json_base64, &json_str)) {
    return Status::InvalidArgument("Invalid base64-encoded JSON");
  }

  doc->Parse<0>(json_str.c_str());
  if (!doc->IsObject()) {
    return Status::InvalidArgument("Invalid JSON", json_str);
  }
  return Status::OK();
}

Status GetTracingOptions(const std::string& json_base64,
                       std::string* category_filter_string,
                       int* tracing_options) {
  rapidjson::Document doc;
  RETURN_NOT_OK(ParseBase64JsonRequest(json_base64, &doc));

  bool use_continuous_tracing = false;
  bool use_sampling = false;

  if (!doc.HasMember("categoryFilter") ||
      !doc["categoryFilter"].IsString()) {
    return Status::InvalidArgument("missing categoryFilter");
  }
  *category_filter_string = doc["categoryFilter"].GetString();

  if (doc.HasMember("useContinuousTracing") &&
      doc["useContinuousTracing"].IsBool()) {
    use_continuous_tracing = doc["useContinuousTracing"].GetBool();
  }

  if (doc.HasMember("useSampling") &&
      doc["useSampling"].IsBool()) {
    use_sampling = doc["useSampling"].GetBool();
  }

  *tracing_options = 0;
  if (use_sampling)
    *tracing_options |= TraceLog::ENABLE_SAMPLING;
  if (use_continuous_tracing)
    *tracing_options |= TraceLog::RECORD_CONTINUOUSLY;
  return Status::OK();
}

Status BeginRecording(const Webserver::WebRequest& req,
                      TraceLog::Mode mode) {
  string filter_str;
  int options;
  RETURN_NOT_OK(GetTracingOptions(req.query_string, &filter_str, &options));

  kudu::debug::TraceLog::GetInstance()->SetEnabled(
    CategoryFilter(filter_str),
    mode,
    static_cast<TraceLog::Options>(options));
  return Status::OK();
}


Status EndRecording(const Webserver::WebRequest& req,
                    stringstream* out) {
  TraceLog* tl = TraceLog::GetInstance();
  tl->SetDisabled();
  *out << TraceResultBuffer::FlushTraceLogToString();
  return Status::OK();
}

Status CaptureMonitoring(stringstream* out) {
  TraceLog* tl = TraceLog::GetInstance();
  if (!tl->IsEnabled()) {
    return Status::IllegalState("monitoring not enabled");
  }
  *out << TraceResultBuffer::FlushTraceLogToStringButLeaveBufferIntact();
  return Status::OK();
}

void GetCategories(stringstream* out) {
  vector<string> groups;
  kudu::debug::TraceLog::GetInstance()->GetKnownCategoryGroups(&groups);
  JsonWriter j(out, JsonWriter::COMPACT);
  j.StartArray();
  for (const string& g : groups) {
    j.String(g);
  }
  j.EndArray();
}

void GetMonitoringStatus(stringstream* out) {
  TraceLog* tl = TraceLog::GetInstance();
  bool is_monitoring = tl->IsEnabled();
  std::string category_filter = tl->GetCurrentCategoryFilter().ToString();
  int options = static_cast<int>(tl->trace_options());

  stringstream json_out;
  JsonWriter j(&json_out, JsonWriter::COMPACT);
  j.StartObject();

  j.String("isMonitoring");
  j.Bool(is_monitoring);

  j.String("categoryFilter");
  j.String(category_filter);

  j.String("useContinuousTracing");
  j.Bool((options & TraceLog::RECORD_CONTINUOUSLY) != 0);

  j.String("useSampling");
  j.Bool((options & TraceLog::ENABLE_SAMPLING) != 0);

  j.EndObject();

  string encoded;
  strings::Base64Escape(json_out.str(), &encoded);
  *out << encoded;
}

void HandleTraceJsonPage(const Webserver::ArgumentMap &args,
                         std::stringstream* output) {
  TraceLog* tl = TraceLog::GetInstance();
  tl->SetEnabled(CategoryFilter(CategoryFilter::kDefaultCategoryFilterString),
                 TraceLog::RECORDING_MODE,
                 TraceLog::RECORD_CONTINUOUSLY);
  SleepFor(MonoDelta::FromSeconds(10));
  tl->SetDisabled();

  *output << TraceResultBuffer::FlushTraceLogToString();
}

Status DoHandleRequest(Handler handler,
                       const Webserver::WebRequest& req,
                       std::stringstream* output) {
  VLOG(2) << "Tracing request type=" << handler << ": " << req.query_string;

  switch (handler) {
    case kBeginMonitoring:
      RETURN_NOT_OK(BeginRecording(req, TraceLog::MONITORING_MODE));
      break;
    case kCaptureMonitoring:
      RETURN_NOT_OK(CaptureMonitoring(output));
      break;
    case kGetMonitoringStatus:
      GetMonitoringStatus(output);
      break;
    case kCategories:
      GetCategories(output);
      break;
    case kBeginRecording:
      RETURN_NOT_OK(BeginRecording(req, TraceLog::RECORDING_MODE));
      break;
    case kGetBufferPercentFull:
      *output << TraceLog::GetInstance()->GetBufferPercentFull();
      break;
    case kEndMonitoring:
    case kEndRecording:
      RETURN_NOT_OK(EndRecording(req, output));
      break;
    case kSimpleDump:
      HandleTraceJsonPage(req.parsed_args, output);
      break;
  }

  return Status::OK();
}


void HandleRequest(Handler handler,
                   const Webserver::WebRequest& req,
                   std::stringstream* output) {
  Status s = DoHandleRequest(handler, req, output);
  if (!s.ok()) {
    LOG(WARNING) << "Tracing error for handler " << handler << ": "
                 << s.ToString();
    // The trace-viewer JS expects '##ERROR##' to indicate that an error
    // occurred. TODO: change the JS to bubble up the actual error message
    // to the user.
    *output << "##ERROR##";
  }
}
} // anonymous namespace


void TracingPathHandlers::RegisterHandlers(Webserver* server) {
  // All of the tracing-related hand
  std::map<string, Handler> handlers = {
    { "/tracing/json/begin_monitoring", kBeginMonitoring },
    { "/tracing/json/end_monitoring", kEndMonitoring },
    { "/tracing/json/capture_monitoring", kCaptureMonitoring },
    { "/tracing/json/get_monitoring_status", kGetMonitoringStatus },
    { "/tracing/json/categories", kCategories },
    { "/tracing/json/begin_recording", kBeginRecording },
    { "/tracing/json/get_buffer_percent_full", kGetBufferPercentFull },
    { "/tracing/json/end_recording", kEndRecording },
    { "/tracing/json/simple_dump", kSimpleDump } };

  typedef pair<string, Handler> HandlerPair;
  for (const HandlerPair& e : handlers) {
    server->RegisterPathHandler(
      e.first, "",
      boost::bind(&HandleRequest, e.second, _1, _2),
      false /* styled */, false /* is_on_nav_bar */);
  }
}

} // namespace server
} // namespace kudu
