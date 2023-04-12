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

#include "opentelemetry/exporters/otlp/otlp_http_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_http_exporter_options.h"

#include "opentelemetry/trace/propagation/detail/hex.h"
#include "opentelemetry/trace/propagation/detail/string.h"
#include "opentelemetry/exporters/ostream/span_exporter_factory.h"

#include "opentelemetry/sdk/common/global_log_handler.h"
#include "opentelemetry/sdk/resource/semantic_conventions.h"
#include "opentelemetry/sdk/trace/simple_processor_factory.h"
#include "opentelemetry/sdk/trace/tracer_provider_factory.h"
#include "opentelemetry/sdk/version/version.h"

#include "opentelemetry/trace/context.h"
#include "opentelemetry/trace/provider.h"
#include "opentelemetry/trace/semantic_conventions.h"
#include "opentelemetry/trace/span_startoptions.h"
#include "opentelemetry/trace/span_metadata.h"
#include "opentelemetry/trace/trace_flags.h"

#include "yb/util/flags.h"
#include "yb/util/otel/ostream_exporter.h"
#include "yb/util/otel/trace.h"

namespace context        = opentelemetry::context;
namespace detail         = opentelemetry::trace::propagation::detail;
namespace trace_api      = opentelemetry::trace;
namespace nostd          = opentelemetry::nostd;
namespace trace_sdk      = opentelemetry::sdk::trace;
namespace trace_exporter = opentelemetry::exporter::trace;
namespace otlp_exporter  = opentelemetry::exporter::otlp;

DEFINE_RUNTIME_bool(
    enable_otel_tracing, false, "Flag to enable/disable OTEL tracing across the code.");
DEFINE_RUNTIME_bool(otel_export_collector, false, "Flag to enable export to OTEL collector");
DEFINE_RUNTIME_string(
    otel_collector_hostname, "127.0.0.1", "IP address of the OTEL collector. Default is 127.0.0.1");

TAG_FLAG(enable_otel_tracing, advanced);
TAG_FLAG(otel_export_collector, advanced);
TAG_FLAG(otel_collector_hostname, advanced);

namespace yb {

static const std::string kPgServiceName = "PG";
static const std::string kTserverServiceName = "TSERVER";

void InitTracer(const std::string& service_name, opentelemetry::sdk::resource::Resource& resource) {
  std::unique_ptr<trace_sdk::SpanExporter> exporter = nullptr;

  if (FLAGS_enable_otel_tracing) {
    if (FLAGS_otel_export_collector) {
      opentelemetry::sdk::common::internal_log::GlobalLogHandler::SetLogLevel(opentelemetry::sdk::common::internal_log::LogLevel::Debug);
      LOG(INFO) << "Setting up exporter";
      otlp_exporter::OtlpHttpExporterOptions opts;
      opts.url = "http://" + FLAGS_otel_collector_hostname + ":4318/v1/traces";

      exporter = otlp_exporter::OtlpHttpExporterFactory::Create(opts);
    } else {
      std::unique_ptr<trace_sdk::SpanExporter> ostream_exporter(new OStreamSpanExporter(FLAGS_log_dir, service_name));
      exporter = std::move(ostream_exporter);
    }

    LOG(INFO) << "Setting up processor";
    auto processor = trace_sdk::SimpleSpanProcessorFactory::Create(std::move(exporter));

    LOG(INFO) << "Setting up provider";
    std::shared_ptr<opentelemetry::trace::TracerProvider> provider =
        trace_sdk::TracerProviderFactory::Create(std::move(processor), resource);

    LOG(INFO) << "Setting up global tracer provider";
    // Set the global trace provider
    trace_api::Provider::SetTracerProvider(provider);

    LOG(INFO) << "Enabled OTEL tracing";
  }
}

void InitPgTracer(int pid) {
  auto resource = opentelemetry::sdk::resource::Resource::Create(
      {{opentelemetry::sdk::resource::SemanticConventions::kServiceName, kPgServiceName},
       {opentelemetry::sdk::resource::SemanticConventions::kProcessPid, std::to_string(pid)}});
  InitTracer(kPgServiceName, resource);
}

void InitTserverTracer(const std::string& host_name) {
  auto resource = opentelemetry::sdk::resource::Resource::Create(
      {{opentelemetry::sdk::resource::SemanticConventions::kServiceName, kTserverServiceName},
       {opentelemetry::trace::SemanticConventions::kNetHostName, host_name}});

  InitTracer(kTserverServiceName, resource);
}

nostd::shared_ptr<opentelemetry::trace::Tracer> get_tracer(std::string tracer_name)
{
  auto provider = opentelemetry::trace::Provider::GetTracerProvider();
  return provider->GetTracer(tracer_name, OPENTELEMETRY_SDK_VERSION);
}

opentelemetry::trace::TraceId TraceIdFromHex(nostd::string_view trace_id)
{
  uint8_t buf[kTraceIdSize / 2];
  detail::HexToBinary(trace_id, buf, sizeof(buf));
  return opentelemetry::trace::TraceId(buf);
}

opentelemetry::trace::SpanId SpanIdFromHex(nostd::string_view span_id)
{
  uint8_t buf[kSpanIdSize / 2];
  detail::HexToBinary(span_id, buf, sizeof(buf));
  return opentelemetry::trace::SpanId(buf);
}

nostd::shared_ptr<opentelemetry::trace::Span> GetParentSpan(
    const std::string& trace_id, const std::string& span_id) {

  // Create a SpanOptions object and set the kind to Server to inform OpenTel.
  trace_api::StartSpanOptions options;
  options.kind = trace_api::SpanKind::kServer;

  auto current_ctx = context::RuntimeContext::GetCurrent();

  auto span_context = trace_api::SpanContext(
      TraceIdFromHex(trace_id), SpanIdFromHex(span_id), trace_api::TraceFlags(), true,
      opentelemetry::trace::TraceState::GetDefault());

  nostd::shared_ptr<trace_api::Span> sp{new trace_api::DefaultSpan(span_context)};
  auto new_context = trace_api::SetSpan(current_ctx, sp);

  options.parent   = trace_api::GetSpan(new_context)->GetContext();

  std::string span_name = "GreeterService/Greet";
  auto span             = get_tracer("grpc")->StartSpan(span_name,
                                            {},
                                            options);
  return span;
}

void CleanupTracer() {
  if (FLAGS_enable_otel_tracing) {
    std::shared_ptr<opentelemetry::trace::TracerProvider> none;
    trace_api::Provider::SetTracerProvider(none);
  }
  LOG(INFO) << "Cleaned up OTEL  tracing";
}

} // namespace yb