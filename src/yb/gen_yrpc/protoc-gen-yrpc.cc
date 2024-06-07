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

#include <functional>

#include <google/protobuf/compiler/code_generator.h>
#include <google/protobuf/compiler/plugin.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/io/printer.h>
#include <google/protobuf/io/zero_copy_stream.h>

#include "yb/gen_yrpc/printer.h"
#include "yb/gen_yrpc/forward_generator.h"
#include "yb/gen_yrpc/messages_generator.h"
#include "yb/gen_yrpc/proxy_generator.h"
#include "yb/gen_yrpc/service_generator.h"
#include "yb/gen_yrpc/substitutions.h"

using namespace std::placeholders;

namespace yb {
namespace gen_yrpc {

template <class T>
struct HasMemberFunction_Source {
  typedef int Yes;
  typedef struct { Yes array[2]; } No;
  typedef typename std::remove_cv<typename std::remove_reference<T>::type>::type CleanedT;

  template <class U>
  static auto Test(YBPrinter* printer, const google::protobuf::FileDescriptor* file, U* u)
      -> decltype(u->Source(*printer, file), Yes(0)) {}
  static No Test(...) {}

  static constexpr bool value =
      sizeof(Test(nullptr, nullptr, static_cast<CleanedT*>(nullptr))) == sizeof(Yes);
};

class CodeGenerator : public google::protobuf::compiler::CodeGenerator {
 public:
  CodeGenerator() { }

  ~CodeGenerator() { }

  bool Generate(const google::protobuf::FileDescriptor *file,
        const std::string& parameter,
        google::protobuf::compiler::GeneratorContext *gen_context,
        std::string *error) const override {

    std::vector<std::pair<std::string, std::string>> params_temp;
    google::protobuf::compiler::ParseGeneratorParameter(parameter, &params_temp);
    std::map<std::string, std::string> params;
    for (const auto& p : params_temp) {
      params.emplace(p.first, p.second);
    }
    bool need_messages = params.count("messages");

    FileSubstitutions name_info(file);

    SubstitutionContext subs;
    subs.Push(name_info.Create());

    Generate<ForwardGenerator>(file, gen_context, &subs, name_info.forward(), need_messages);

    if (file->service_count() != 0) {
      Generate<ServiceGenerator>(file, gen_context, &subs, name_info.service());
      Generate<ProxyGenerator>(file, gen_context, &subs, name_info.proxy());
    }

    if (need_messages) {
      Generate<MessagesGenerator>(
          file, gen_context, &subs, name_info.messages());
    }

    return true;
  }

 private:
  template <class Generator, class... Args>
  void Generate(
      const google::protobuf::FileDescriptor *file,
      google::protobuf::compiler::GeneratorContext *gen_context,
      SubstitutionContext *subs, const std::string& fname,
      Args&&... args) const {
    Generator generator(std::forward<Args>(args)...);
    DoGenerate(
        file, gen_context, subs, fname + ".h",
        std::bind(&Generator::Header, &generator, _1, _2));

    GenerateSource(&generator, file, gen_context, subs, fname + ".cc");
  }

  template <class Generator>
  typename std::enable_if<HasMemberFunction_Source<Generator>::value, void>::type GenerateSource(
      Generator* generator,
      const google::protobuf::FileDescriptor *file,
      google::protobuf::compiler::GeneratorContext *gen_context,
      SubstitutionContext *subs, const std::string& fname) const {
    DoGenerate(
        file, gen_context, subs, fname,
        std::bind(&Generator::Source, generator, _1, _2));
  }

  template <class Generator, class... Args>
  typename std::enable_if<!HasMemberFunction_Source<Generator>::value, void>::type GenerateSource(
      Generator* generator, Args&&... args) const {}

  template <class F>
  void DoGenerate(
      const google::protobuf::FileDescriptor *file,
      google::protobuf::compiler::GeneratorContext *gen_context,
      SubstitutionContext *subs, const std::string& fname, const F& generator) const {
    std::unique_ptr<google::protobuf::io::ZeroCopyOutputStream> output(gen_context->Open(fname));
    google::protobuf::io::Printer printer(output.get(), '$');
    YBPrinter yb_printer(&printer, subs);
    generator(yb_printer, file);
  }
};

} // namespace gen_yrpc
} // namespace yb

int main(int argc, char *argv[]) {
  yb::gen_yrpc::CodeGenerator generator;
  return google::protobuf::compiler::PluginMain(argc, argv, &generator);
}
