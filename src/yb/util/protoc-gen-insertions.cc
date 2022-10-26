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
// Simple protoc plugin which inserts some code at the top of each generated protobuf.
// Currently, this just adds an include of protobuf-annotations.h, a file which hooks up
// the protobuf concurrency annotations to our TSAN annotations.
#include <google/protobuf/compiler/code_generator.h>
#include <google/protobuf/compiler/plugin.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/io/printer.h>
#include <google/protobuf/io/zero_copy_stream.h>

#include "yb/gutil/strings/strip.h"
#include "yb/gutil/strings/substitute.h"

using std::string;

using google::protobuf::io::ZeroCopyOutputStream;
using google::protobuf::io::Printer;

namespace yb {

static const char* const kIncludeToInsert = "#include \"yb/util/protobuf-annotations.h\"\n";
static const char* const kProtoExtension = ".proto";

class InsertAnnotations : public ::google::protobuf::compiler::CodeGenerator {
  virtual bool Generate(const google::protobuf::FileDescriptor *file,
                        const std::string &/*param*/,
                        google::protobuf::compiler::GeneratorContext *gen_context,
                        std::string *error) const override {

    // Determine the file name we will substitute into.
    string path_no_extension;
    if (!TryStripSuffixString(file->name(), kProtoExtension, &path_no_extension)) {
      *error = strings::Substitute("file name $0 did not end in $1", file->name(), kProtoExtension);
      return false;
    }
    string pb_file = path_no_extension + ".pb.cc";

    // Actually insert the new #include
    std::unique_ptr<ZeroCopyOutputStream> inserter(gen_context->OpenForInsert(pb_file, "includes"));
    Printer printer(inserter.get(), '$');
    printer.Print(kIncludeToInsert);

    if (printer.failed()) {
      *error = "Failed to print to output file";
      return false;
    }

    return true;
  }
};

} // namespace yb

int main(int argc, char *argv[]) {
  yb::InsertAnnotations generator;
  return google::protobuf::compiler::PluginMain(argc, argv, &generator);
}
