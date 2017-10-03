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

////////////////////////////////////////////////////////////////////////////////
// Example usage:
// protoc --plugin=protoc-gen-krpc --krpc_out . --proto_path . <file>.proto
////////////////////////////////////////////////////////////////////////////////

#include <ctype.h>
#include <glog/logging.h>
#include <google/protobuf/compiler/code_generator.h>
#include <google/protobuf/compiler/plugin.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/io/printer.h>
#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/stubs/common.h>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>

#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/strings/split.h"
#include "kudu/gutil/strings/join.h"
#include "kudu/gutil/strings/numbers.h"
#include "kudu/gutil/strings/strip.h"
#include "kudu/gutil/strings/stringpiece.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/gutil/strings/util.h"
#include "kudu/util/status.h"
#include "kudu/util/string_case.h"

using google::protobuf::FileDescriptor;
using google::protobuf::io::Printer;
using google::protobuf::MethodDescriptor;
using google::protobuf::ServiceDescriptor;
using std::map;
using std::shared_ptr;
using std::string;
using std::vector;

namespace kudu {
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
      return Status::InvalidArgument("file name " + path +
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

  virtual void InitSubstitutionMap(map<string, string> *map) const OVERRIDE {
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

  virtual void InitSubstitutionMap(map<string, string> *map) const OVERRIDE {
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
    StringPiece service_package(service_full_name);
    if (!service_package.contains(".")) {
      return arg_full_name;
    }
    // remove the service name so that we are left with only the package, including
    // the last '.' so that we account for different packages with the same prefix.
    service_package.remove_suffix(service_package.length() -
                                  service_package.find_last_of(".") - 1);

    StringPiece argfqn(arg_full_name);
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

  virtual void InitSubstitutionMap(map<string, string> *map) const OVERRIDE {
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
        std::string *error) const OVERRIDE {
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
      "#ifndef KUDU_RPC_$upper_case$_SERVICE_IF_DOT_H\n"
      "#define KUDU_RPC_$upper_case$_SERVICE_IF_DOT_H\n"
      "\n"
      "#include \"$path_no_extension$.pb.h\"\n"
      "\n"
      "#include <string>\n"
      "\n"
      "#include \"kudu/rpc/rpc_header.pb.h\"\n"
      "#include \"kudu/rpc/service_if.h\"\n"
      "\n"
      "namespace kudu {\n"
      "class MetricEntity;\n"
      "namespace rpc {\n"
      "class Messenger;\n"
      "class RpcContext;\n"
      "} // namespace rpc\n"
      "} // namespace kudu\n"
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
        "class $service_name$If : public ::kudu::rpc::ServiceIf {\n"
        " public:\n"
        "  explicit $service_name$If(const scoped_refptr<MetricEntity>& entity);\n"
        "  virtual ~$service_name$If();\n"
        "  virtual void Handle(::kudu::rpc::InboundCall *call);\n"
        "  virtual std::string service_name() const;\n"
        "  static std::string static_service_name();\n"
        "\n"
        );

      for (int method_idx = 0; method_idx < service->method_count();
           ++method_idx) {
        const MethodDescriptor *method = service->method(method_idx);
        subs->PushMethod(method);

        Print(printer, *subs,
        "  virtual void $rpc_name$(const $request$ *req,\n"
        "     $response$ *resp, ::kudu::rpc::RpcContext *context) = 0;\n"
        );

        subs->Pop();
      }

      Print(printer, *subs,
        "\n"
        " private:\n"
      );


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
        "  static const int kMethodCount = $service_method_count$;\n"
        "\n"
        "  // Pre-initialize metrics because calling METRIC_foo.Instantiate() is expensive.\n"
        "  void InitMetrics(const scoped_refptr<MetricEntity>& ent);\n"
        "\n"
        "  ::kudu::rpc::RpcMethodMetrics metrics_[kMethodCount];\n"
        "\n"
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
      "#include \"kudu/rpc/inbound_call.h\"\n"
      "#include \"kudu/rpc/remote_method.h\"\n"
      "#include \"kudu/rpc/rpc_context.h\"\n"
      "#include \"kudu/rpc/service_if.h\"\n"
      "#include \"kudu/util/metrics.h\"\n"
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
          "METRIC_DEFINE_histogram(server, handler_latency_$rpc_full_name_plainchars$,\n"
          "  \"$rpc_full_name$ RPC Time\",\n"
          "  kudu::MetricUnit::kMicroseconds,\n"
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
        "void $service_name$If::Handle(::kudu::rpc::InboundCall *call) {\n"
        "  {\n");

      for (int method_idx = 0; method_idx < service->method_count();
           ++method_idx) {
        const MethodDescriptor *method = service->method(method_idx);
        subs->PushMethod(method);

        Print(printer, *subs,
        "    if (call->remote_method().method_name() == \"$rpc_name$\") {\n"
        "      $request$ *req = new $request$;\n"
        "      if (PREDICT_FALSE(!ParseParam(call, req))) {\n"
        "        delete req;\n"
        "        return;\n"
        "      }\n"
        "      $response$ *resp = new $response$;\n"
        "      $rpc_name$(req, resp,\n"
        "          new ::kudu::rpc::RpcContext(call, req, resp,\n"
        "                                      metrics_[$metric_enum_key$]));\n"
        "      return;\n"
        "    }\n"
        "\n");
        subs->Pop();
      }
      Print(printer, *subs,
        "  }\n"
        "  RespondBadMethod(call);\n"
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
      "#ifndef KUDU_RPC_$upper_case$_PROXY_DOT_H\n"
      "#define KUDU_RPC_$upper_case$_PROXY_DOT_H\n"
      "\n"
      "#include \"$path_no_extension$.pb.h\"\n"
      "\n"
      "#include \"kudu/rpc/proxy.h\"\n"
      "#include \"kudu/util/status.h\"\n"
      "\n"
      "namespace kudu { class Sockaddr; }\n"
      "namespace kudu { namespace rpc { class UserCredentials; } }\n"
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
        "  $service_name$Proxy(const std::shared_ptr< ::kudu::rpc::Messenger>\n"
        "                &messenger, const ::kudu::Sockaddr &sockaddr);\n"
        "  ~$service_name$Proxy();\n"
        "\n"
        "  // Set the user information for the connection.\n"
        "  void set_user_credentials(const ::kudu::rpc::UserCredentials& user_credentials);\n"
        "\n"
        "  // Get the current user information for the connection.\n"
        "  const ::kudu::rpc::UserCredentials& user_credentials() const;\n"
        "\n"
        );

      for (int method_idx = 0; method_idx < service->method_count();
           ++method_idx) {
        const MethodDescriptor *method = service->method(method_idx);
        subs->PushMethod(method);

        Print(printer, *subs,
        "\n"
        "  ::kudu::Status $rpc_name$(const $request$ &req, $response$ *resp,\n"
        "                          ::kudu::rpc::RpcController *controller);\n"
        "  void $rpc_name$Async(const $request$ &req,\n"
        "                       $response$ *response,\n"
        "                       ::kudu::rpc::RpcController *controller,\n"
        "                       const ::kudu::rpc::ResponseCallback &callback);\n"
        );
        subs->Pop();
      }
      Print(printer, *subs,
      " private:\n"
      "  ::kudu::rpc::Proxy proxy_;\n"
      "};\n");
      subs->Pop();
    }
    Print(printer, *subs,
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
      "#include \"kudu/rpc/outbound_call.h\"\n"
      "#include \"kudu/util/net/sockaddr.h\"\n"
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
        "   const std::shared_ptr< ::kudu::rpc::Messenger> &messenger,\n"
        "   const ::kudu::Sockaddr &remote)\n"
        "  : proxy_(messenger, remote, \"$full_service_name$\") {\n"
        "}\n"
        "\n"
        "$service_name$Proxy::~$service_name$Proxy() {\n"
        "}\n"
        "\n"
        "void $service_name$Proxy::set_user_credentials(\n"
        "  const ::kudu::rpc::UserCredentials& user_credentials) {\n"
        "  proxy_.set_user_credentials(user_credentials);\n"
        "}\n"
        "\n"
        "const ::kudu::rpc::UserCredentials& $service_name$Proxy::user_credentials() const {\n"
        "  return proxy_.user_credentials();\n"
        "}\n"
        "\n");
      for (int method_idx = 0; method_idx < service->method_count();
           ++method_idx) {
        const MethodDescriptor *method = service->method(method_idx);
        subs->PushMethod(method);
        Print(printer, *subs,
        "::kudu::Status $service_name$Proxy::$rpc_name$(const $request$ &req, $response$ *resp,\n"
        "                                     ::kudu::rpc::RpcController *controller) {\n"
        "  return proxy_.SyncRequest(\"$rpc_name$\", req, resp, controller);\n"
        "}\n"
        "\n"
        "void $service_name$Proxy::$rpc_name$Async(const $request$ &req,\n"
        "                     $response$ *resp, ::kudu::rpc::RpcController *controller,\n"
        "                     const ::kudu::rpc::ResponseCallback &callback) {\n"
        "  proxy_.AsyncRequest(\"$rpc_name$\", req, resp, controller, callback);\n"
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
} // namespace kudu

int main(int argc, char *argv[]) {
  kudu::rpc::CodeGenerator generator;
  return google::protobuf::compiler::PluginMain(argc, argv, &generator);
}
