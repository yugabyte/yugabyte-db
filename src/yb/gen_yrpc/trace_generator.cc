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


#include "yb/gen_yrpc/trace_generator.h"

#include <google/protobuf/descriptor.h>
#include <google/protobuf/io/printer.h>

using namespace std::literals;

namespace yb {
namespace gen_yrpc {

namespace {

// Signatures of the per-message functions, declared in the header and defined in the source.
constexpr char kTracingAttributesSignature[] =
    "std::vector<std::pair<std::string, std::string>> TracingAttributes(\n"
    "    const $message_pb_name$& msg)";
constexpr char kAppendTracingAttributesSignature[] =
    "void AppendTracingAttributes(\n"
    "    const $message_pb_name$& msg, const std::string& prefix,\n"
    "    ::yb::rpc::TracingAttributeCollector* out)";

// Messages with trace-tagged fields, nested ones first, skipping map entries (their pb type is
// MapPair, which has no free function).
void CollectTracedMessages(
    const google::protobuf::Descriptor* message,
    std::vector<const google::protobuf::Descriptor*>* out) {
  for (int i = 0; i != message->nested_type_count(); ++i) {
    CollectTracedMessages(message->nested_type(i), out);
  }
  if (!message->options().map_entry() && MessageHasTracingAttributes(message)) {
    out->push_back(message);
  }
}

std::vector<const google::protobuf::Descriptor*> TracedMessages(
    const google::protobuf::FileDescriptor* file) {
  std::vector<const google::protobuf::Descriptor*> result;
  for (int i = 0; i != file->message_type_count(); ++i) {
    CollectTracedMessages(file->message_type(i), &result);
  }
  return result;
}

} // namespace

void TracingAttributesBody(
    YBPrinter printer, const google::protobuf::Descriptor* message, Lightweight lightweight) {
  const bool lw = lightweight;
  const std::string obj = lw ? std::string() : "msg."s;

  ScopedIndent method_indent(printer);

  for (int j = 0; j != message->field_count(); ++j) {
    const auto* field = message->field(j);
    const bool nested = IsMessage(field) && MessageHasTracingAttributes(field->message_type());
    const bool scalar = !IsMessage(field) && FieldTraceEnabled(field);
    if (!nested && !scalar) {
      continue;
    }
    ScopedSubstituter field_substituter(printer, field);

    // Value expression, given the token yielding a single field value (e.g. "$field_name$()" or
    // "msg.$field_name$()" for a singular field, "entry" for a repeated element).
    auto value_expr = [field, lw](const std::string& token) -> std::string {
      if (StoredAsSlice(field)) {
        const bool hex = field->type() == google::protobuf::FieldDescriptor::TYPE_BYTES &&
                         !FieldTraceBytesAsString(field);
        if (lw) {
          return token + (hex ? ".ToDebugHexString()" : ".ToBuffer()");
        }
        return hex ? "::yb::Slice(" + token + ").ToDebugHexString()" : token;
      }
      if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_ENUM) {
        return "$nonlw_field_type$_Name(" + token + ")";
      }
      if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_BOOL) {
        return "std::string(" + token + " ? \"true\" : \"false\")";
      }
      return "std::to_string(" + token + ")";
    };

    if (!field->is_repeated()) {
      // The lightweight message always has has_$field$(); the pb_ message only for fields with
      // explicit presence -- guard the rest on non-emptiness (string/bytes) or emit them plainly.
      if (lw || field->has_presence()) {
        printer("if (" + obj + "has_$field_name$()) {\n");
      } else if (StoredAsSlice(field)) {
        printer("if (!" + obj + "$field_name$().empty()) {\n");
      } else {
        printer("{\n");
      }
      ScopedIndent if_indent(printer);
      if (nested) {
        if (lw) {
          printer("$field_name$().AppendTracingAttributes(prefix + \"$field_name$.\", out);\n");
        } else {
          printer(
              "AppendTracingAttributes(" + obj +
              "$field_name$(), prefix + \"$field_name$.\", out);\n");
        }
      } else {
        printer(
            "out->Add(prefix + \"$field_name$\", " +
            value_expr(obj + "$field_name$()") + ");\n");
      }
      if_indent.Reset("}\n");
      continue;
    }

    // Repeated: every element shares one key with `*` for the index; the collector merges the
    // distinct values.
    const bool pointer = lw && IsPointerField(field);
    if (pointer) {
      printer("if ($field_accessor$) {\n");
      printer.printer().Indent();
    }
    const std::string range =
        lw ? (pointer ? "*$field_accessor$"s : "$field_accessor$"s) : "msg.$field_name$()"s;
    printer("{\n");
    ScopedIndent loop_indent(printer);
    printer(
        "const auto entry_prefix = prefix + \"$field_name$.*"s + (nested ? "." : "") + "\";\n");
    printer("for (const auto& entry : " + range + ") {\n");
    {
      ScopedIndent body_indent(printer);
      if (nested) {
        printer(
            (lw ? "entry.AppendTracingAttributes(entry_prefix, out);\n"s
                : "AppendTracingAttributes(entry, entry_prefix, out);\n"s));
      } else {
        printer("out->Add(entry_prefix, " + value_expr("entry") + ");\n");
      }
    }
    printer("}\n");
    loop_indent.Reset("}\n");
    if (pointer) {
      printer.printer().Outdent();
      printer("}\n");
    }
  }

  method_indent.Reset("}\n\n");
}

void TraceGenerator::Header(YBPrinter printer, const google::protobuf::FileDescriptor* file) {
  printer(
      "// THIS FILE IS AUTOGENERATED FROM $path$\n"
      "\n"
      "#pragma once\n"
      "\n"
  );

  // A request type often lives in an imported file (tserver_service.proto takes tserver.proto's
  // messages), and a nested field of an imported type recurses into that file's functions. Pull
  // in the imports' declarations here so one include covers either case.
  bool has_deps = false;
  for (int i = 0; i != file->dependency_count(); ++i) {
    const auto* dep = file->dependency(i);
    if (!TracedMessages(dep).empty()) {
      printer("#include \"" + RemoveProtoExtension(dep->name()) + ".trace.h\"\n");
      has_deps = true;
    }
  }
  if (has_deps) {
    printer("\n");
  }

  const auto messages = TracedMessages(file);
  if (messages.empty()) {
    return;
  }

  printer(
      "#include <string>\n"
      "#include <utility>\n"
      "#include <vector>\n"
      "\n"
      "#include \"$path_no_extension$.pb.h\"\n"
      "\n"
      "namespace yb {\n"
      "namespace rpc {\n"
      "class TracingAttributeCollector;\n"
      "}\n"
      "}\n"
      "\n"
      "$open_namespace$\n"
  );

  for (const auto* message : messages) {
    ScopedSubstituter message_substituter(printer, message);
    printer(kTracingAttributesSignature + ";\n"s);
    printer(kAppendTracingAttributesSignature + ";\n\n"s);
  }

  printer("$close_namespace$");
}

void TraceGenerator::Source(YBPrinter printer, const google::protobuf::FileDescriptor* file) {
  printer(
      "// THIS FILE IS AUTOGENERATED FROM $path$\n"
      "\n"
  );

  // Nothing to define: skip the header, whose import includes drag in their .pb.h files.
  const auto messages = TracedMessages(file);
  if (messages.empty()) {
    return;
  }

  printer(
      "#include \"$path_no_extension$.trace.h\"\n"
      "\n"
      "#include \"yb/rpc/lightweight_message.h\"\n"
      "\n"
      "$open_namespace$\n"
  );

  for (const auto* message : messages) {
    ScopedSubstituter message_substituter(printer, message);
    printer(
        kTracingAttributesSignature +
        " {\n"
        "  ::yb::rpc::TracingAttributeCollector collector;\n"
        "  AppendTracingAttributes(msg, \"req.\", &collector);\n"
        "  return collector.Finish();\n"
        "}\n"
        "\n"s);
    printer(kAppendTracingAttributesSignature + " {\n"s);
    TracingAttributesBody(printer, message, Lightweight::kFalse);
  }

  printer("$close_namespace$");
}

}  // namespace gen_yrpc
}  // namespace yb
