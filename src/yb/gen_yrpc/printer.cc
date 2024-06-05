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

#include "yb/gen_yrpc/printer.h"

#include <google/protobuf/io/printer.h>

namespace yb {
namespace gen_yrpc {

void SubstitutionContext::Push(const Substitutions& subs) {
  for (const auto& p : subs) {
    map_.emplace(p.first, p.second);
  }
}

void SubstitutionContext::Pop(const Substitutions& subs) {
  for (const auto& p : subs) {
    map_.erase(p.first);
  }
}

YBPrinter::YBPrinter(google::protobuf::io::Printer* printer, SubstitutionContext* subs)
    : printer_(printer), subs_(subs) {
}

void YBPrinter::operator()(const char* text) {
  printer_->Print(subs_->map(), text);
}

void YBPrinter::Indent() {
  printer_->Indent();
}

void YBPrinter::Outdent() {
  printer_->Outdent();
}

ScopedIndent::ScopedIndent(const YBPrinter& printer)
    : printer_(&printer.printer()) {
  printer_->Indent();
}

ScopedIndent::~ScopedIndent() {
  Reset();
}

void ScopedIndent::Reset(const char* suffix) {
  if (printer_) {
    printer_->Outdent();
    if (suffix) {
      printer_->Print(suffix);
    }
    printer_ = nullptr;
  }
}

ScopedSubstituter::ScopedSubstituter(SubstitutionContext* context, Substitutions substitutions)
    : context_(context), substitutions_(std::move(substitutions)) {
  context_->Push(substitutions_);
}

ScopedSubstituter::ScopedSubstituter(YBPrinter printer, Substitutions substitutions)
    : ScopedSubstituter(printer.subs(), std::move(substitutions)) {
}

ScopedSubstituter::~ScopedSubstituter() {
  context_->Pop(substitutions_);
}

} // namespace gen_yrpc
} // namespace yb
