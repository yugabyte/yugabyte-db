// Copyright (c) YugaByte, Inc.
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

#include "yb/gen_yrpc/metric_descriptor.h"

#include <google/protobuf/descriptor.h>

#include "yb/gen_yrpc/model.h"

namespace yb {
namespace gen_yrpc {

Substitutions MetricDescriptor::CreateSubstitutions() const {
  Substitutions result;
  result.emplace_back("metric_name", name);
  result.emplace_back("metric_prefix", prefix);
  result.emplace_back("metric_kind", kind);
  result.emplace_back("metric_extra_args", extra_args);
  result.emplace_back("metric_units", units);
  result.emplace_back("metric_description", description);
  return result;
}

void GenerateMethodIndexesEnum(
    YBPrinter printer, const google::protobuf::ServiceDescriptor* service) {
  printer("enum class $service_method_enum$ {\n");
  for (int method_idx = 0; method_idx < service->method_count(); ++method_idx) {
    ScopedSubstituter method_subs(printer, service->method(method_idx), rpc::RpcSides::SERVICE);

    printer("  $metric_enum_key$,\n");
  }

  printer("}; // enum\n");
}

void GenerateMetricDefines(
    YBPrinter printer, const google::protobuf::FileDescriptor* file,
    const std::vector<MetricDescriptor>& metric_descriptors) {
  for (int service_idx = 0; service_idx < file->service_count(); ++service_idx) {
    const auto* service = file->service(service_idx);
    ScopedSubstituter service_subs(printer, service);

    for (int method_idx = 0; method_idx < service->method_count(); ++method_idx) {
      ScopedSubstituter method_subs(printer, service->method(method_idx), rpc::RpcSides::SERVICE);

      for (const auto& desc : metric_descriptors) {
        ScopedSubstituter metric_subs(printer, desc.CreateSubstitutions());
        std::string text =
          "METRIC_DEFINE_$metric_kind$(\n  server,"
          " $metric_prefix$$metric_name$_$rpc_full_name_plainchars$,\n"
          "  \"$metric_description$ $rpc_full_name$() RPC requests\",\n"
          "  $metric_units$,\n"
          "  \"$metric_description$ $rpc_full_name$() RPC requests\"$metric_extra_args$);\n"
          "\n";
        printer(text);
      }
    }
  }
}

void GenerateHandlerAssignment(
    YBPrinter printer, const google::protobuf::MethodDescriptor* method) {
  printer(".handler = [this](::yb::rpc::InboundCallPtr call) {\n");
  ScopedIndent handler_indent(printer);
  printer(
      "call->SetRpcMethodMetrics(methods_["
          "static_cast<size_t>($service_method_enum$::$metric_enum_key$)].metrics);\n"
      "::yb::rpc::HandleCall<::yb::rpc::$params$Impl<$request$, $response$>>(\n"
      "    std::move(call), [this](const $request$* req, $response$* resp, "
          "::yb::rpc::RpcContext rpc_context) {\n");
  if (IsTrivialMethod(method)) {
    ScopedIndent callback_indent(printer);
    printer(
        "auto result = $rpc_name$(*req, rpc_context.GetClientDeadline());\n"
        "if (result.ok()) {\n"
        "  resp->Swap(result.get_ptr());\n"
        "} else {\n"
        "  SetupError(::yb::rpc::ResponseError(resp), result.status());\n"
        "}\n"
        "rpc_context.RespondSuccess();\n"
    );
  } else {
    printer("  $rpc_name$(req, resp, std::move(rpc_context));\n");
  }
  printer(
      "});\n"
  );
  handler_indent.Reset("},\n");
}

void GenerateMethodAssignments(
    YBPrinter printer, const google::protobuf::ServiceDescriptor* service,
    const std::string &mutable_metric_fmt, bool service_side,
    const std::vector<MetricDescriptor>& metric_descriptors) {
  ScopedIndent indent(printer);

  for (int method_idx = 0; method_idx < service->method_count(); ++method_idx) {
    auto* method = service->method(method_idx);
    ScopedSubstituter method_subs(printer, method, rpc::RpcSides::SERVICE);

    printer(mutable_metric_fmt + " = {\n");
    if (service_side) {
      ScopedIndent method_indent(printer);
      printer(".method = ::yb::rpc::RemoteMethod(\"$full_service_name$\", \"$rpc_name$\"),\n");
      GenerateHandlerAssignment(printer, method);
      printer(".metrics = ::yb::rpc::RpcMethodMetrics(\n");
    }
    bool first = true;
    for (const auto& desc : metric_descriptors) {
      if (first) {
         first = false;
      } else {
        printer(",\n");
      }
      ScopedSubstituter metric_subs(printer, desc.CreateSubstitutions());
      if (service_side) {
        printer("  ");
      }
      printer(
          "    METRIC_$metric_prefix$$metric_name$_$rpc_full_name_plainchars$.Instantiate(entity)");
    }
    printer((service_side ? ")" : std::string()) + "\n};\n\n");
  }
}

} // namespace gen_yrpc
} // namespace yb
