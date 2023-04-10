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

#include "yb/util/otel_trace.h"
#include "yb/util/otel_ostream_exporter.h"
#include "opentelemetry/exporters/ostream/common_utils.h"

#include <fstream>
#include <map>
#include <mutex>
#include "opentelemetry/sdk_config.h"

namespace nostd     = opentelemetry::nostd;
namespace trace_sdk = opentelemetry::sdk::trace;
namespace trace_api = opentelemetry::trace;
namespace sdkcommon = opentelemetry::sdk::common;

namespace yb
{

std::ostream &operator<<(std::ostream &os, trace_api::SpanKind span_kind)
{
  switch (span_kind)
  {
    case trace_api::SpanKind::kClient:
      return os << "Client";
    case trace_api::SpanKind::kInternal:
      return os << "Internal";
    case trace_api::SpanKind::kServer:
      return os << "Server";
    case trace_api::SpanKind::kProducer:
      return os << "Producer";
    case trace_api::SpanKind::kConsumer:
      return os << "Consumer";
  };
  return os << "";
}

OStreamSpanExporter::OStreamSpanExporter(
    const std::string &base_path, const std::string &file_prefix) noexcept
    :  base_path_(base_path), file_prefix_(file_prefix) {}

std::unique_ptr<trace_sdk::Recordable> OStreamSpanExporter::MakeRecordable() noexcept
{
  return std::unique_ptr<trace_sdk::Recordable>(new trace_sdk::SpanData);
}

opentelemetry::sdk::common::ExportResult OStreamSpanExporter::Export(
    const nostd::span<std::unique_ptr<trace_sdk::Recordable>> &spans) noexcept
{
  if (isShutdown())
  {
    OTEL_INTERNAL_LOG_ERROR("[Ostream Trace Exporter] Exporting "
                            << spans.size() << " span(s) failed, exporter is shutdown");
    return opentelemetry::sdk::common::ExportResult::kFailure;
  }

  std::map<std::string, std::shared_ptr<std::ofstream>> ostream_map;

  for (auto &recordable : spans)
  {
    auto span = std::unique_ptr<trace_sdk::SpanData>(
        static_cast<trace_sdk::SpanData *>(recordable.release()));

    if (span != nullptr)
    {

      char trace_id[32]       = {0};
      char span_id[16]        = {0};
      char parent_span_id[16] = {0};
      std::shared_ptr<std::ofstream> ostream_ptr = nullptr;

      span->GetTraceId().ToLowerBase16(trace_id);
      span->GetSpanId().ToLowerBase16(span_id);
      span->GetParentSpanId().ToLowerBase16(parent_span_id);

      std::string trace_id_str(trace_id, kTraceIdSize);
      if (auto search = ostream_map.find(trace_id_str);
          search != ostream_map.end()) {
        ostream_ptr = search->second;
      } else {
        std::string file_name = base_path_ + "/" + file_prefix_ + "-" + trace_id_str + ".log";
        ostream_ptr = std::make_shared<std::ofstream>(std::ofstream(file_name.c_str(), std::ios_base::app));
        ostream_map.insert(std::make_pair(trace_id_str, ostream_ptr));
      }

      std::ostream &sout_ = *ostream_ptr.get();

      sout_ << "{"
            << "\n  name          : " << span->GetName()
            << "\n  trace_id      : " << std::string(trace_id, 32)
            << "\n  span_id       : " << std::string(span_id, 16)
            << "\n  tracestate    : " << span->GetSpanContext().trace_state()->ToHeader()
            << "\n  parent_span_id: " << std::string(parent_span_id, 16)
            << "\n  start         : " << span->GetStartTime().time_since_epoch().count()
            << "\n  duration      : " << span->GetDuration().count()
            << "\n  description   : " << span->GetDescription()
            << "\n  span kind     : " << span->GetSpanKind()
            << "\n  status        : " << statusMap[int(span->GetStatus())]
            << "\n  attributes    : ";
      printAttributes(sout_, span->GetAttributes());
      sout_ << "\n  events        : ";
      printEvents(sout_, span->GetEvents());
      sout_ << "\n  links         : ";
      printLinks(sout_, span->GetLinks());
      sout_ << "\n  resources     : ";
      printResources(sout_, span->GetResource());
      sout_ << "\n  instr-lib     : ";
      printInstrumentationScope(sout_, span->GetInstrumentationScope());
      sout_ << "\n}\n";
    }
  }

  // close all the ostreams
  for (const auto & [file_name, fd] : ostream_map)
    fd->close();
  return opentelemetry::sdk::common::ExportResult::kSuccess;
}

bool OStreamSpanExporter::Shutdown(std::chrono::microseconds /* timeout */) noexcept
{
  const std::lock_guard<opentelemetry::common::SpinLockMutex> locked(lock_);
  is_shutdown_ = true;
  return true;
}

bool OStreamSpanExporter::isShutdown() const noexcept
{
  const std::lock_guard<opentelemetry::common::SpinLockMutex> locked(lock_);
  return is_shutdown_;
}
void OStreamSpanExporter::printAttributes(
    std::ostream& sout_,
    const std::unordered_map<std::string, sdkcommon::OwnedAttributeValue> &map,
    const std::string prefix)
{
  for (const auto &kv : map)
  {
    sout_ << prefix << kv.first << ": ";
    opentelemetry::exporter::ostream_common::print_value(kv.second, sout_);
  }
}

void OStreamSpanExporter::printEvents(std::ostream& sout_, const std::vector<trace_sdk::SpanDataEvent> &events)
{
  for (const auto &event : events)
  {
    sout_ << "\n\t{"
          << "\n\t  name          : " << event.GetName()
          << "\n\t  timestamp     : " << event.GetTimestamp().time_since_epoch().count()
          << "\n\t  attributes    : ";
    printAttributes(sout_, event.GetAttributes(), "\n\t\t");
    sout_ << "\n\t}";
  }
}

void OStreamSpanExporter::printLinks(std::ostream& sout_, const std::vector<trace_sdk::SpanDataLink> &links)
{
  for (const auto &link : links)
  {
    char trace_id[32] = {0};
    char span_id[16]  = {0};
    link.GetSpanContext().trace_id().ToLowerBase16(trace_id);
    link.GetSpanContext().span_id().ToLowerBase16(span_id);
    sout_ << "\n\t{"
          << "\n\t  trace_id      : " << std::string(trace_id, 32)
          << "\n\t  span_id       : " << std::string(span_id, 16)
          << "\n\t  tracestate    : " << link.GetSpanContext().trace_state()->ToHeader()
          << "\n\t  attributes    : ";
    printAttributes(sout_, link.GetAttributes(), "\n\t\t");
    sout_ << "\n\t}";
  }
}

void OStreamSpanExporter::printResources(std::ostream& sout_, const opentelemetry::sdk::resource::Resource &resources)
{
  auto attributes = resources.GetAttributes();
  if (attributes.size())
  {
    printAttributes(sout_,attributes, "\n\t");
  }
}

void OStreamSpanExporter::printInstrumentationScope(
    std::ostream& sout_,
    const opentelemetry::sdk::instrumentationscope::InstrumentationScope &instrumentation_scope)
{
  sout_ << instrumentation_scope.GetName();
  auto version = instrumentation_scope.GetVersion();
  if (version.size())
  {
    sout_ << "-" << version;
  }
}

}  // namespace yb