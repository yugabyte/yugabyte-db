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

#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "yb/gen_yrpc/gen_yrpc_fwd.h"
#include "yb/gen_yrpc/substitutions.h"

namespace yb {
namespace gen_yrpc {

class SubstitutionContext {
 public:
  void Push(const Substitutions& subs);
  void Pop(const Substitutions& subs);

  const std::map<std::string, std::string>& map() const {
    return map_;
  }

 private:
  std::map<std::string, std::string> map_;
};

class YBPrinter {
 public:
  YBPrinter(google::protobuf::io::Printer* printer, SubstitutionContext* subs);

  void operator()(const char* text);

  void operator()(const std::string& text) {
    (*this)(text.c_str());
  }

  void Indent();
  void Outdent();

  SubstitutionContext* subs() {
    return subs_;
  }

  google::protobuf::io::Printer& printer() const {
    return *printer_;
  }

 private:
  google::protobuf::io::Printer* printer_;
  SubstitutionContext* subs_;
};

class ScopedIndent {
 public:
  explicit ScopedIndent(const YBPrinter& printer);

  ScopedIndent(const ScopedIndent&) = delete;
  void operator=(const ScopedIndent&) = delete;

  ~ScopedIndent();

  void Reset(const char* suffix = nullptr);

 private:
  google::protobuf::io::Printer* printer_;
};

class ScopedSubstituter {
 public:
  template <class... Args>
  ScopedSubstituter(SubstitutionContext* context, Args&&... args)
      : ScopedSubstituter(context, CreateSubstitutions(std::forward<Args>(args)...)) {
  }

  template <class... Args>
  ScopedSubstituter(YBPrinter printer, Args&&... args)
      : ScopedSubstituter(printer.subs(), std::forward<Args>(args)...) {
  }

  ScopedSubstituter(SubstitutionContext* context, Substitutions substitutions);
  ScopedSubstituter(YBPrinter printer, Substitutions substitutions);

  ScopedSubstituter(const ScopedSubstituter&) = delete;
  void operator=(const ScopedSubstituter&) = delete;

  ~ScopedSubstituter();

 private:
  SubstitutionContext* context_;
  Substitutions substitutions_;
};

} // namespace gen_yrpc
} // namespace yb
