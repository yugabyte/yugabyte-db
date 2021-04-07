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

////////////////////////////////////////////////////////////////////////////////
// Example usage:
// protoc --plugin=protoc-gen-yrpc --yrpc_out . --proto_path . <file>.proto
////////////////////////////////////////////////////////////////////////////////

#include <ctype.h>

#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>

#include <glog/logging.h>
#include <google/protobuf/compiler/code_generator.h>
#include <google/protobuf/compiler/plugin.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/io/printer.h>
#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/stubs/common.h>

#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/strings/split.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/numbers.h"
#include "yb/gutil/strings/strip.h"
#include "yb/gutil/strings/stringpiece.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/strings/util.h"
#include "yb/util/status.h"
#include "yb/util/string_case.h"

using google::protobuf::FileDescriptor;
using google::protobuf::io::Printer;
using google::protobuf::MethodDescriptor;
using google::protobuf::ServiceDescriptor;
using std::map;
using std::shared_ptr;
using std::string;
using std::vector;

namespace yb {
namespace rpc {

class Substituter {
 public:
  virtual ~Substituter() {}
  virtual void InitSubstitutionMap(map<string, string> *map) const = 0;
};

// NameInfo contains information about the output names.
class FileSubstitutions : public Substituter {
 public:
  static const std::string PROTO_EXTENSION;

  Status Init(const FileDescriptor *file) {
    string path = file->name();
    map_["path"] = path;

    // Initialize path_
    // If path = /foo/bar/baz_stuff.proto, path_ = /foo/bar/baz_stuff
    if (!TryStripSuffixString(path, PROTO_EXTENSION, &path_no_extension_)) {
      return STATUS(InvalidArgument, "file name " + path +
                                     " did not end in " + PROTO_EXTENSION);
    }
    map_["path_no_extension"] = path_no_extension_;

    // If path = /foo/bar/baz_stuff.proto, base_ = baz_stuff
    string base;
    GetBaseName(path_no_extension_, &base);
    map_["base"] = base;

    // If path = /foo/bar/baz_stuff.proto, camel_case_ = BazStuff
    string camel_case;
    SnakeToCamelCase(base, &camel_case);
    map_["camel_case"] = camel_case;

    // If path = /foo/bar/baz_stuff.proto, upper_case_ = BAZ_STUFF
    string upper_case;
    ToUpperCase(base, &upper_case);
    map_["upper_case"] = upper_case;

    map_["open_namespace"] = GenerateOpenNamespace(file->package());
    map_["close_namespace"] = GenerateCloseNamespace(file->package());

    return Status::OK();
  }

  void InitSubstitutionMap(map<string, string> *map) const override {
    typedef std::map<string, string>::value_type kv_pair;
    for (const kv_pair &pair : map_) {
      (*map)[pair.first] = pair.second;
    }
  }

  std::string service_header() const {
    return path_no_extension_ + ".service.h";
  }

  std::string service() const {
    return path_no_extension_ + ".service.cc";
  }

  std::string proxy_header() const {
    return path_no_extension_ + ".proxy.h";
  }

  std::string proxy() const {
    return path_no_extension_ + ".proxy.cc";
  }

 private:
  // Extract the last filename component.
  static void GetBaseName(const string &path,
                          string *base) {
    size_t last_slash = path.find_last_of("/");
    if (last_slash != string::npos) {
      *base = path.substr(last_slash + 1);
    } else {
      *base = path;
    }
  }

  static string GenerateOpenNamespace(const string &str) {
    vector<string> components = strings::Split(str, ".");
    string out;
    for (const string &c : components) {
      out.append("namespace ").append(c).append(" {\n");
    }
    return out;
  }

  static string GenerateCloseNamespace(const string &str) {
    vector<string> components = strings::Split(str, ".");
    string out;
    for (auto c = components.crbegin(); c != components.crend(); c++) {
      out.append("} // namespace ").append(*c).append("\n");
    }
    return out;
  }

  std::string path_no_extension_;
  map<string, string> map_;
};

const std::string FileSubstitutions::PROTO_EXTENSION(".proto");

class MethodSubstitutions : public Substituter {
 public:
  explicit MethodSubstitutions(const MethodDescriptor *method)
    : method_(method) {
  }

  void InitSubstitutionMap(map<string, string> *map) const override {
    (*map)["rpc_name"] = method_->name();
    (*map)["rpc_full_name"] = method_->full_name();
    (*map)["rpc_full_name_plainchars"] =
        StringReplace(method_->full_name(), ".", "_", true);
    (*map)["request"] =
        ReplaceNamespaceDelimiters(
            StripNamespaceIfPossible(method_->service()->full_name(),
                                     method_->input_type()->full_name()));
    (*map)["response"] =
        ReplaceNamespaceDelimiters(
            StripNamespaceIfPossible(method_->service()->full_name(),
                                     method_->output_type()->full_name()));
    (*map)["metric_enum_key"] = strings::Substitute("kMetricIndex$0", method_->name());
  }

  // Strips the package from method arguments if they are in the same package as
  // the service, otherwise leaves them so that we can have fully qualified
  // namespaces for method arguments.
  static std::string StripNamespaceIfPossible(const std::string& service_full_name,
                                              const std::string& arg_full_name) {
    GStringPiece service_package(service_full_name);
    if (!service_package.contains(".")) {
      return arg_full_name;
    }
    // remove the service name so that we are left with only the package, including
    // the last '.' so that we account for different packages with the same prefix.
    service_package.remove_suffix(service_package.length() -
                                  service_package.find_last_of(".") - 1);

    GStringPiece argfqn(arg_full_name);
    if (argfqn.starts_with(service_package)) {
      argfqn.remove_prefix(argfqn.find_last_of(".") + 1);
    }
    return argfqn.ToString();
  }

  static std::string ReplaceNamespaceDelimiters(const std::string& arg_full_name) {
    return JoinStrings(strings::Split(arg_full_name, "."), "::");
  }

 private:
  const MethodDescriptor *method_;
};

class ServiceSubstitutions : public Substituter {
 public:
  explicit ServiceSubstitutions(const ServiceDescriptor *service)
    : service_(service)
  {}

  void InitSubstitutionMap(map<string, string> *map) const override {
    (*map)["service_name"] = service_->name();
    (*map)["full_service_name"] = service_->full_name();
    (*map)["service_method_count"] = SimpleItoa(service_->method_count());

    // TODO: upgrade to protobuf 2.5.x and attach service comments
    // to the generated service classes using the SourceLocation API.
  }

 private:
  const ServiceDescriptor *service_;
};


class SubstitutionContext {
 public:
  // Takes ownership of the substituter
  void Push(const Substituter *sub) {
    subs_.push_back(shared_ptr<const Substituter>(sub));
  }

  void PushMethod(const MethodDescriptor *method) {
    Push(new MethodSubstitutions(method));
  }

  void PushService(const ServiceDescriptor *service) {
    Push(new ServiceSubstitutions(service));
  }

  void Pop() {
    CHECK(!subs_.empty());
    subs_.pop_back();
  }

  void InitSubstitutionMap(map<string, string> *subs) const {
    for (const shared_ptr<const Substituter> &sub : subs_) {
      sub->InitSubstitutionMap(subs);
    }
  }

 private:
  vector<shared_ptr<const Substituter> > subs_;
};



class CodeGenerator : public ::google::protobuf::compiler::CodeGenerator {
 public:
  CodeGenerator() { }

  ~CodeGenerator() { }

  bool Generate(const google::protobuf::FileDescriptor *file,
        const std::string &/* parameter */,
        google::protobuf::compiler::GeneratorContext *gen_context,
        std::string *error) const override {
    auto name_info = new FileSubstitutions();
    Status ret = name_info->Init(file);
    if (!ret.ok()) {
      *error = "name_info.Init failed: " + ret.ToString();
      return false;
    }

    SubstitutionContext subs;
    subs.Push(name_info);

    gscoped_ptr<google::protobuf::io::ZeroCopyOutputStream> ih_output(
        gen_context->Open(name_info->service_header()));
    Printer ih_printer(ih_output.get(), '$');
    GenerateServiceIfHeader(&ih_printer, &subs, file);

    gscoped_ptr<google::protobuf::io::ZeroCopyOutputStream> i_output(
        gen_context->Open(name_info->service()));
    Printer i_printer(i_output.get(), '$');
    GenerateServiceIf(&i_printer, &subs, file);

    gscoped_ptr<google::protobuf::io::ZeroCopyOutputStream> ph_output(
        gen_context->Open(name_info->proxy_header()));
    Printer ph_printer(ph_output.get(), '$');
    GenerateProxyHeader(&ph_printer, &subs, file);

    gscoped_ptr<google::protobuf::io::ZeroCopyOutputStream> p_output(
        gen_context->Open(name_info->proxy()));
    Printer p_printer(p_output.get(), '$');
    GenerateProxy(&p_printer, &subs, file);

    return true;
  }

 private:
  void Print(Printer *printer,
             const SubstitutionContext &sub,
             const char *text) const {
    map<string, string> subs;
    sub.InitSubstitutionMap(&subs);
    printer->Print(subs, text);
  }

  void GenerateServiceIfHeader(Printer *printer,
                               SubstitutionContext *subs,
                               const FileDescriptor *file) const {
    Print(printer, *subs,
      "// THIS FILE IS AUTOGENERATED FROM $path$\n"
      "\n"
      "#ifndef YB_RPC_$upper_case$_SERVICE_IF_DOT_H\n"
      "#define YB_RPC_$upper_case$_SERVICE_IF_DOT_H\n"
      "\n"
      "#include \"$path_no_extension$.pb.h\"\n"
      "\n"
      "#include <string>\n"
      "\n"
      "#include \"yb/rpc/rpc_fwd.h\"\n"
      "#include \"yb/rpc/rpc_header.pb.h\"\n"
      "#include \"yb/rpc/service_if.h\"\n"
      "\n"
      "namespace yb {\n"
      "class MetricEntity;\n"
      "} // namespace yb\n"
      "\n"
      "$open_namespace$"
      "\n"
      );

    for (int service_idx = 0; service_idx < file->service_count();
         ++service_idx) {
      const ServiceDescriptor *service = file->service(service_idx);
      subs->PushService(service);

      Print(printer, *subs,
        "\n"
        "class $service_name$If : public ::yb::rpc::ServiceIf {\n"
        " public:\n"
        "  explicit $service_name$If(const scoped_refptr<MetricEntity>& entity);\n"
        "  virtual ~$service_name$If();\n"
        "  virtual void Handle(::yb::rpc::InboundCallPtr call);\n"
        "  virtual std::string service_name() const;\n"
        "  static std::string static_service_name();\n"
        "\n"
        );

      for (int method_idx = 0; method_idx < service->method_count();
           ++method_idx) {
        const MethodDescriptor *method = service->method(method_idx);
        subs->PushMethod(method);

        Print(printer, *subs,
        "  virtual void $rpc_name$(\n"
        "      const $request$ *req,\n"
        "      $response$ *resp,\n"
        "      ::yb::rpc::RpcContext context) = 0;\n"
        );

        subs->Pop();
      }

      Print(printer, *subs,
            "  enum RpcMetricIndexes {\n"
      );
      for (int method_idx = 0; method_idx < service->method_count();
          ++method_idx) {
        const MethodDescriptor *method = service->method(method_idx);
        subs->PushMethod(method);

        Print(printer, *subs,
              "    $metric_enum_key$,\n"
        );

        subs->Pop();
      }

      Print(printer, *subs,
            "  };\n" // enum
      );

      Print(printer, *subs,
            "\n"
                "  ::yb::rpc::RpcMethodMetrics GetMetric(RpcMetricIndexes index) {\n"
                "    return metrics_[index];\n"
                "  }\n"
      );

      Print(printer, *subs,
        "\n"
        " private:\n"
      );

      Print(printer, *subs,
        "  static const int kMethodCount = $service_method_count$;\n"
        "\n"
        "  // Pre-initialize metrics because calling METRIC_foo.Instantiate() is expensive.\n"
        "  void InitMetrics(const scoped_refptr<MetricEntity>& ent);\n"
        "\n"
        "  ::yb::rpc::RpcMethodMetrics metrics_[kMethodCount];\n"
        "};\n"
      );

      subs->Pop(); // Service
    }

    Print(printer, *subs,
      "\n"
      "$close_namespace$\n"
      "#endif\n");
  }

  void GenerateServiceIf(Printer *printer,
                         SubstitutionContext *subs,
                         const FileDescriptor *file) const {
    Print(printer, *subs,
      "// THIS FILE IS AUTOGENERATED FROM $path$\n"
      "\n"
      "#include \"$path_no_extension$.pb.h\"\n"
      "#include \"$path_no_extension$.service.h\"\n"
      "\n"
      "#include <glog/logging.h>\n"
      "\n"
      "#include \"yb/rpc/inbound_call.h\"\n"
      "#include \"yb/rpc/local_call.h\"\n"
      "#include \"yb/rpc/remote_method.h\"\n"
      "#include \"yb/rpc/rpc_context.h\"\n"
      "#include \"yb/rpc/service_if.h\"\n"
      "#include \"yb/util/metrics.h\"\n"
      "\n");

    // Define metric prototypes for each method in the service.
    for (int service_idx = 0; service_idx < file->service_count();
        ++service_idx) {
      const ServiceDescriptor *service = file->service(service_idx);
      subs->PushService(service);

      for (int method_idx = 0; method_idx < service->method_count();
          ++method_idx) {
        const MethodDescriptor *method = service->method(method_idx);
        subs->PushMethod(method);
        Print(printer, *subs,
          "METRIC_DEFINE_histogram_with_percentiles(server,"
          " handler_latency_$rpc_full_name_plainchars$,\n"
          "  \"$rpc_full_name$ RPC Time\",\n"
          "  yb::MetricUnit::kMicroseconds,\n"
          "  \"Microseconds spent handling $rpc_full_name$() RPC requests\",\n"
          "  60000000LU, 2);\n"
          "\n");
        subs->Pop();
      }

      subs->Pop();
    }

    Print(printer, *subs,
      "$open_namespace$"
      "\n");

    for (int service_idx = 0; service_idx < file->service_count();
         ++service_idx) {
      const ServiceDescriptor *service = file->service(service_idx);
      subs->PushService(service);

      Print(printer, *subs,
        "$service_name$If::$service_name$If(const scoped_refptr<MetricEntity>& entity) {\n"
        "  InitMetrics(entity);\n"
        "}\n"
        "\n"
        "$service_name$If::~$service_name$If() {\n"
        "}\n"
        "\n"
        "void $service_name$If::Handle(::yb::rpc::InboundCallPtr call) {\n"
        "  auto yb_call = std::static_pointer_cast<::yb::rpc::YBInboundCall>(call);\n");

      for (int method_idx = 0; method_idx < service->method_count();
           ++method_idx) {
        const MethodDescriptor *method = service->method(method_idx);
        subs->PushMethod(method);

        Print(printer, *subs,
        "  if (call->method_name() == \"$rpc_name$\") {\n"
        "    auto rpc_context = yb_call->IsLocalCall() ?\n"
        "        ::yb::rpc::RpcContext(\n"
        "            std::static_pointer_cast<::yb::rpc::LocalYBInboundCall>(yb_call), \n"
        "            metrics_[$metric_enum_key$]) :\n"
        "        ::yb::rpc::RpcContext(\n"
        "            yb_call, \n"
        "            std::make_shared<$request$>(),\n"
        "            std::make_shared<$response$>(),\n"
        "            metrics_[$metric_enum_key$]);\n"
        "    if (!rpc_context.responded()) {\n"
        "      const auto* req = static_cast<const $request$*>(rpc_context.request_pb());\n"
        "      auto* resp = static_cast<$response$*>(rpc_context.response_pb());\n"
        "      $rpc_name$(req, resp, std::move(rpc_context));\n"
        "    }\n"
        "    return;\n"
        "  }\n"
        "\n");
        subs->Pop();
      }
      Print(printer, *subs,
        "  yb_call->RespondBadMethod();\n"
        "}\n"
        "\n"
        "std::string $service_name$If::service_name() const {\n"
        "  return \"$full_service_name$\";\n"
        "}\n"
        "std::string $service_name$If::static_service_name() {\n"
        "  return \"$full_service_name$\";\n"
        "}\n"
        "\n"
      );

      Print(printer, *subs,
        "void $service_name$If::InitMetrics(const scoped_refptr<MetricEntity>& entity) {\n"
      );
      // Expose per-RPC metrics.
      for (int method_idx = 0; method_idx < service->method_count();
           ++method_idx) {
        const MethodDescriptor *method = service->method(method_idx);
        subs->PushMethod(method);

        Print(printer, *subs,
          "  metrics_[$metric_enum_key$].handler_latency = \n"
          "      METRIC_handler_latency_$rpc_full_name_plainchars$.Instantiate(entity);\n"
        );

        subs->Pop();
      }
      Print(printer, *subs,
        "}\n"
        "\n"
      );

      subs->Pop();
    }

    Print(printer, *subs,
      "$close_namespace$"
      );
  }

  void GenerateProxyHeader(Printer *printer,
                           SubstitutionContext *subs,
                           const FileDescriptor *file) const {
    Print(printer, *subs,
      "// THIS FILE IS AUTOGENERATED FROM $path$\n"
      "\n"
      "#ifndef YB_RPC_$upper_case$_PROXY_DOT_H\n"
      "#define YB_RPC_$upper_case$_PROXY_DOT_H\n"
      "\n"
      "#include \"$path_no_extension$.pb.h\"\n"
      "\n"
      "#include \"yb/rpc/proxy.h\"\n"
      "#include \"yb/util/status.h\"\n"
      "#include \"yb/util/net/net_fwd.h\"\n"
      "\n"
      "namespace yb {\n"
      "namespace rpc {\n"
      "class Proxy;\n"
      "}\n"
      "}\n"
      "\n"
      "$open_namespace$"
      "\n"
      "\n"
    );

    for (int service_idx = 0; service_idx < file->service_count();
         ++service_idx) {
      const ServiceDescriptor *service = file->service(service_idx);
      subs->PushService(service);

      Print(printer, *subs,
        "class $service_name$Proxy {\n"
        " public:\n"
        "  $service_name$Proxy(\n"
        "      ::yb::rpc::ProxyCache* cache,\n"
        "      const ::yb::HostPort &endpoint,\n"
        "      const ::yb::rpc::Protocol* protocol = nullptr,\n"
        "      const boost::optional<MonoDelta>& resolve_cache_timeout = boost::none);\n"
        "  ~$service_name$Proxy();\n"
        "\n"
        );

      for (int method_idx = 0; method_idx < service->method_count();
           ++method_idx) {
        const MethodDescriptor *method = service->method(method_idx);
        subs->PushMethod(method);

        Print(printer, *subs,
        "\n"
        "  ::yb::Status $rpc_name$(const $request$ &req, $response$ *resp,\n"
        "                          ::yb::rpc::RpcController *controller);\n"
        "  void $rpc_name$Async(const $request$ &req,\n"
        "                       $response$ *response,\n"
        "                       ::yb::rpc::RpcController *controller,\n"
        "                       ::yb::rpc::ResponseCallback callback);\n"
        );
        subs->Pop();
      }
      subs->Pop();
    }
    Print(printer, *subs,
      "  ::yb::rpc::Proxy& proxy() { return *proxy_; }\n"
      "\n"
      "private:\n"
      "  std::shared_ptr<::yb::rpc::Proxy> const proxy_;\n"
      "};\n"
      "\n"
      "$close_namespace$"
      "\n"
      "#endif\n"
      );
  }

  void GenerateProxy(Printer *printer,
                     SubstitutionContext *subs,
                     const FileDescriptor *file) const {
    Print(printer, *subs,
      "// THIS FILE IS AUTOGENERATED FROM $path$\n"
      "\n"
      "#include \"$path_no_extension$.proxy.h\"\n"
      "\n"
      "#include \"yb/rpc/proxy.h\"\n"
      "#include \"yb/rpc/outbound_call.h\"\n"
      "#include \"yb/util/net/sockaddr.h\"\n"
      "\n"
      "$open_namespace$"
      "\n"
      );

    for (int service_idx = 0; service_idx < file->service_count();
         ++service_idx) {
      const ServiceDescriptor *service = file->service(service_idx);
      subs->PushService(service);
      Print(printer, *subs,
        "$service_name$Proxy::$service_name$Proxy(\n"
        "   ::yb::rpc::ProxyCache* cache,\n"
        "   const ::yb::HostPort &remote,\n"
        "   const ::yb::rpc::Protocol* protocol,\n"
        "   const boost::optional<MonoDelta>& resolve_cache_timeout)\n"
        "  : proxy_(cache->Get(remote, protocol, resolve_cache_timeout)) {\n"
        "}\n"
        "\n"
        "$service_name$Proxy::~$service_name$Proxy() {\n"
        "}\n"
        "\n");
      for (int method_idx = 0; method_idx < service->method_count();
           ++method_idx) {
        const MethodDescriptor *method = service->method(method_idx);
        subs->PushMethod(method);
        Print(printer, *subs,
        "::yb::Status $service_name$Proxy::$rpc_name$(const $request$ &req, $response$ *resp,\n"
        "                                     ::yb::rpc::RpcController *controller) {\n"
        "  static ::yb::rpc::RemoteMethod method(\"$full_service_name$\", \"$rpc_name$\");\n"
        "  return proxy_->SyncRequest(&method, req, resp, controller);\n"
        "}\n"
        "\n"
        "void $service_name$Proxy::$rpc_name$Async(const $request$ &req,\n"
        "                     $response$ *resp, ::yb::rpc::RpcController *controller,\n"
        "                     ::yb::rpc::ResponseCallback callback) {\n"
        "  static ::yb::rpc::RemoteMethod method(\"$full_service_name$\", \"$rpc_name$\");\n"
        "  proxy_->AsyncRequest(&method, req, resp, controller, std::move(callback));\n"
        "}\n"
        "\n");
        subs->Pop();
      }

      subs->Pop();
    }
    Print(printer, *subs,
      "$close_namespace$");
  }
};
} // namespace rpc
} // namespace yb

int main(int argc, char *argv[]) {
  yb::rpc::CodeGenerator generator;
  return google::protobuf::compiler::PluginMain(argc, argv, &generator);
}
