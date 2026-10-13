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


#pragma once

#include "yb/gen_yrpc/model.h"
#include "yb/gen_yrpc/printer.h"

namespace yb {
namespace gen_yrpc {

// Emits the body of AppendTracingAttributes for `message`: one out->Add per (yb.rpc.trace)
// field, recursing into nested messages that carry tagged fields. `lightweight` picks the
// lightweight member form (fields via `this`) or the pb free-function form (fields via `msg`).
void TracingAttributesBody(
    YBPrinter printer, const google::protobuf::Descriptor* message, Lightweight lightweight);

// Generates $path$.trace.{h,cc}: TracingAttributes(const PB&) free functions for every message
// in the file with trace-tagged fields. Produced for every proto file, so the pb proxy stub can
// drain request attributes without the file opting into lightweight messages.
class TraceGenerator {
 public:
  void Header(YBPrinter printer, const google::protobuf::FileDescriptor* file);
  void Source(YBPrinter printer, const google::protobuf::FileDescriptor* file);
};

}  // namespace gen_yrpc
}  // namespace yb
