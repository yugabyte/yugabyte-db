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

#include "yb/gen_yrpc/messages_generator.h"

#include <boost/algorithm/string/predicate.hpp>
#include <google/protobuf/io/printer.h>

#include "yb/gen_yrpc/model.h"

using google::protobuf::internal::WireFormatLite;

namespace yb {
namespace gen_yrpc {

namespace {

void NextCtorField(YBPrinter printer, bool* first) {
  if (*first) {
    printer("\n    : ");
    *first = false;
  } else {
    printer(",\n      ");
  }
}

class Message {
 public:
  explicit Message(const google::protobuf::Descriptor* message)
      : message_(message) {
  }

  bool generated() const {
    return generated_;
  }

  void CycleDependency(const google::protobuf::FieldDescriptor* field) {
    cycle_dependencies_.insert(field);
  }

  void Declaration(YBPrinter printer) {
    ScopedSubstituter message_substituter(printer, message_);

    for (int j = 0; j != message_->field_count(); ++j) {
      const auto* field = message_->field(j);
      if (field->is_repeated() || StoreAsPointer(field)) {
        continue;
      }
      if (!need_has_fields_enum_) {
        need_has_fields_enum_ = true;
        printer("YB_DEFINE_ENUM($message_name$Fields,\n");
      }
      ScopedSubstituter field_substituter(printer, field);

      printer("  (k$field_camelcase_name$)\n");
    }

    if (need_has_fields_enum_) {
      printer(");\n\n");
    }

    printer(
        "class $message_lw_name$ : public ::yb::rpc::LightweightMessage {\n"
        " public:\n"
    );

    ScopedIndent public_scope(printer);

    printer(
        "explicit $message_lw_name$(::yb::Arena* arena);\n"
        "$message_lw_name$(::yb::Arena* arena, const $message_lw_name$& rhs);\n"
        "\n"
        "$message_lw_name$(::yb::Arena* arena, const $message_pb_name$& rhs) \n"
        "    : $message_lw_name$(arena) {\n"
        "  CopyFrom(rhs);\n"
        "}\n"
        "\n"
        "void operator=(const $message_lw_name$& rhs) {\n"
        "  CopyFrom(rhs);\n"
        "}\n"
        "\n"
        "void operator=(const $message_pb_name$& rhs) {\n"
        "  CopyFrom(rhs);\n"
        "}\n"
        "\n"
        "CHECKED_STATUS ParseFromCodedStream("
            "google::protobuf::io::CodedInputStream* cis) override;\n"
        "size_t SerializedSize() const override;\n"
        "uint8_t* SerializeToArray(uint8_t* out) const override;\n"
        "void AppendToDebugString(std::string* out) const override;\n"
        "\n"
        "void Clear() override;\n"
        "void CopyFrom(const $message_lw_name$& rhs);\n"
        "void CopyFrom(const $message_pb_name$& rhs);\n"
        "\n"
    );

    if (!message_->options().map_entry()) {
      printer(
          "void ToGoogleProtobuf($message_pb_name$* out) const;\n"
          "\n"
          "$message_pb_name$ ToGoogleProtobuf() const {\n"
          "  $message_pb_name$ result;\n"
          "  ToGoogleProtobuf(&result);\n"
          "  return result;\n"
          "}\n"
          "\n"
      );
    }

    bool has_nested_using = false;
    for (auto i = 0; i != message_->enum_type_count(); ++i) {
      const auto* nested = message_->enum_type(i);
      printer("using " + nested->name() + " = $message_name$::" + nested->name() + ";\n");
      has_nested_using = true;
    }

    for (auto i = 0; i != message_->nested_type_count(); ++i) {
      const auto* nested = message_->nested_type(i);
      printer("using " + MakeLightweightName(nested->name()) + " = " +
              RelativeClassPath(UnnestedName(nested, Lightweight::kTrue, true),
                                message_->full_name()) + ";\n");
      has_nested_using = true;
    }

    if (has_nested_using) {
      printer("\n");
    }

    for (int j = 0; j != message_->field_count(); ++j) {
      const auto* field = message_->field(j);
      ScopedSubstituter field_substituter(printer, field);

      if (IsSimple(field)) {
        printer(
            "$field_stored_type$ $field_name$() const {\n"
            "  return $field_name$_;\n"
            "}\n\n"
        );
        if (StoredAsSlice(field)) {
          printer(
              "void dup_$field_name$($field_stored_type$ value) {\n"
              "  has_fields_.Set($message_name$Fields::k$field_camelcase_name$);\n"
              "  $field_name$_ = arena_.DupSlice(value);\n"
              "}\n\n"
              "void ref_$field_name$($field_stored_type$ value) {\n"
              "  has_fields_.Set($message_name$Fields::k$field_camelcase_name$);\n"
              "  $field_name$_ = value;\n"
              "}\n\n"
          );
        } else {
          printer(
              "void set_$field_name$($field_stored_type$ value) {\n"
              "  has_fields_.Set($message_name$Fields::k$field_camelcase_name$);\n"
              "  $field_name$_ = value;\n"
              "}\n\n"
          );
        }
      } else if (StoreAsPointer(field)) {
        printer(
            "const $field_stored_type$& $field_name$() const {\n"
            "  return $field_name$_ ? *$field_name$_ "
                ": ::yb::rpc::empty_message<$field_stored_type$>();\n"
            "}\n\n"
            "$field_stored_type$& ref_$field_name$($field_stored_type$* value) {\n"
            "  $field_name$_ = value;\n"
            "  return *$field_name$_;\n"
            "}\n\n"
        );
      } else {
        printer(
            "const $field_stored_type$& $field_name$() const {\n"
            "  return $field_name$_;\n"
            "}\n\n"
        );
      }

      printer(
          "$field_stored_type$* mutable_$field_name$() {\n"
      );

      ScopedIndent mutable_ident(printer);

      if (StoreAsPointer(field)) {
        printer(
          "if (!$field_name$_) {\n"
          "  $field_name$_ = arena_.NewObject<$field_stored_type$>(&arena_);\n"
          "}\n"
          "return $field_name$_;\n"
        );
      } else {
        if (!field->is_repeated()) {
          printer(
              "has_fields_.Set($message_name$Fields::k$field_camelcase_name$);\n"
          );
        }

        printer(
            "return &$field_name$_;\n"
        );
      }

      mutable_ident.Reset("}\n\n");

      if (!field->is_repeated()) {
        printer("bool has_$field_name$() const {\n");
        if (StoreAsPointer(field)) {
          printer("  return $field_name$_ != nullptr;\n");
        } else {
          printer("  return has_fields_.Test($message_name$Fields::k$field_camelcase_name$);\n");
        }
        printer("}\n\n");
        printer("void clear_$field_name$() {\n");
        if (StoreAsPointer(field)) {
          printer("  $field_name$_ = nullptr;\n");
        } else {
          if (IsMessage(field)) {
            printer("  $field_name$_.Clear();\n");
          } else {
            printer("  $field_name$_ = $field_stored_type$();\n");
          }
          printer("  has_fields_.Reset($message_name$Fields::k$field_camelcase_name$);\n");
        }
        printer("}\n\n");
      } else if (IsMessage(field)) {
        printer(
            "$field_type$* add_$field_name$() {\n"
            "  return &$field_name$_.emplace_back();\n"
            "}\n\n"
        );
      }
    }

    if (NeedArena(message_)) {
      printer(
          "::yb::Arena& arena() const {\n"
          "  return arena_;"
          "}\n\n"
      );
    }

    printer(
        "size_t cached_size() const {\n"
        "  return cached_size_;\n"
        "}\n\n"
    );

    public_scope.Reset();

    printer(" private:\n");

    ScopedIndent private_scope(printer);

    if (NeedArena(message_)) {
      printer(
          "::yb::Arena& arena_;\n"
      );
    }
    if (need_has_fields_enum_) {
      printer(
          "::yb::EnumBitSet<$message_name$Fields> has_fields_;\n"
      );
    }
    printer(
        "mutable size_t cached_size_ = 0;\n"
    );

    for (int j = 0; j != message_->field_count(); ++j) {
      const auto* field = message_->field(j);
      ScopedSubstituter field_substituter(printer, field);

      if (StoreAsPointer(field)) {
        printer(
            "$field_stored_type$* $field_name$_ = nullptr;\n"
        );
      } else if (IsSimple(field)) {
        printer(
            "$field_stored_type$ $field_name$_ = $field_stored_type$();\n"
        );
      } else {
        printer(
            "$field_stored_type$ $field_name$_;\n"
        );
        if (field->is_packed() && !FixedSize(field)) {
          printer(
              "mutable size_t $field_name$_cached_size_ = 0;\n"
          );
        }
      }
    }

    private_scope.Reset("};\n\n");

    generated_ = true;
  }

  void Definition(YBPrinter printer) const {
    ScopedSubstituter message_substituter(printer, message_);

    Ctor(printer);
    CopyCtor(printer);
    AppendToDebugString(printer);
    Clear(printer);
    CopyFrom(printer, Lightweight::kTrue);
    CopyFrom(printer, Lightweight::kFalse);
    Parse(printer);
    Serialize(printer);
    Size(printer);
    if (!message_->options().map_entry()) {
      ToGoogleProtobuf(printer);
    }
  }

 private:
  void Ctor(YBPrinter printer) const {
    printer("$message_lw_name$::$message_lw_name$(::yb::Arena* arena)");
    bool first = true;
    if (NeedArena(message_)) {
      NextCtorField(printer, &first);
      printer("arena_(*arena)");
    }
    for (int j = 0; j != message_->field_count(); ++j) {
      const auto* field = message_->field(j);
      if (!field->is_repeated() && (!IsMessage(field) || StoreAsPointer(field))) {
        continue;
      }
      NextCtorField(printer, &first);
      ScopedSubstituter field_substituter(printer, field);
      printer("$field_name$_(arena)");
    }
    printer(" {\n}\n\n");
  }

  void CopyCtor(YBPrinter printer) const {
    printer(
        "$message_lw_name$::$message_lw_name$(::yb::Arena* arena, const $message_lw_name$& rhs)"
    );
    bool first = true;
    if (NeedArena(message_)) {
      NextCtorField(printer, &first);
      printer("arena_(*arena)");
    }
    if (need_has_fields_enum_) {
      NextCtorField(printer, &first);
      printer("has_fields_(rhs.has_fields_)");
    }
    for (int j = 0; j != message_->field_count(); ++j) {
      NextCtorField(printer, &first);
      const auto* field = message_->field(j);
      ScopedSubstituter field_substituter(printer, field);
      if (StoreAsPointer(field)) {
        printer(
            "$field_name$_(rhs.$field_name$_ "
                "? arena->NewObject<$field_stored_type$>(arena, *rhs.$field_name$_) : nullptr)"
        );
      } else if (IsMessage(field)) {
        printer("$field_name$_(arena, rhs.$field_name$_)");
      } else if (field->is_repeated()) {
        if (StoredAsSlice(field)) {
          printer("$field_name$_(arena)");
        } else {
          printer("$field_name$_(rhs.$field_name$_.begin(), rhs.$field_name$_.end(), arena)");
        }
      } else if (StoredAsSlice(field)) {
        printer("$field_name$_(arena->DupSlice(rhs.$field_name$_))");
      } else {
        printer("$field_name$_(rhs.$field_name$_)");
      }
    }
    printer(" {\n");
    ScopedIndent body_indent(printer);
    for (int j = 0; j != message_->field_count(); ++j) {
      const auto* field = message_->field(j);
      ScopedSubstituter field_substituter(printer, field);
      if (!field->is_repeated() || !StoredAsSlice(field)) {
        continue;
      }
      printer(
          "$field_name$_.reserve(rhs.$field_name$_.size());\n"
          "for (const auto& entry : rhs.$field_name$_) {\n"
          "  $field_name$_.push_back(arena->DupSlice(entry));\n"
          "}\n"
      );
    }
    body_indent.Reset("}\n\n");
  }

  void AppendToDebugString(YBPrinter printer) const {
    printer("void $message_lw_name$::AppendToDebugString(std::string* out) const {\n");
    ScopedIndent indent(printer);
    if (message_->field_count()) {
      printer("bool first = true;");
    }
    std::vector<const google::protobuf::FieldDescriptor*> fields;
    for (int j = 0; j != message_->field_count(); ++j) {
      fields.push_back(message_->field(j));
    }
    std::sort(fields.begin(), fields.end(), [](const auto* lhs, const auto* rhs) {
      return lhs->number() < rhs->number();
    });
    for (const auto* field : fields) {
      ScopedSubstituter field_substituter(printer, field);
      if (field->is_repeated()) {
        printer("for (const auto& entry : $field_name$_) {\n");
      } else {
        printer("if (has_$field_name$()) {\n");
      }
      if (IsMessage(field)) {
        printer("  ::yb::rpc::AppendFieldTitle(\"$field_name$\", \" { \", &first, out);\n");
        printer("  $field_value$.AppendToDebugString(out);\n");
        printer("  *out += \" }\";\n");
      } else if (StoredAsSlice(field)) {
        printer("  ::yb::rpc::AppendFieldTitle(\"$field_name$\", \": \\\"\", &first, out);\n");
        printer("  *out += $field_value$.ToBuffer();\n");
        printer("  *out += '\"';\n");
      } else {
        printer("  ::yb::rpc::AppendFieldTitle(\"$field_name$\", \": \", &first, out);\n");
        if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE) {
          printer("  *out += ::SimpleDtoa($field_value$);\n");
        } else if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_FLOAT) {
          printer("  *out += ::SimpleFtoa($field_value$);\n");
        } else if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_ENUM) {
          printer("  *out += $nonlw_field_type$_Name($field_value$);\n");
        } else {
          printer("  *out += std::to_string($field_value$);\n");
        }
      }
      printer("}\n");
    }
    indent.Reset("}\n\n");
  }

  void Clear(YBPrinter printer) const {
    printer("void $message_lw_name$::Clear() {\n");
    for (int j = 0; j != message_->field_count(); ++j) {
      const auto* field = message_->field(j);
      ScopedSubstituter field_substituter(printer, field);
      if (field->is_repeated()) {
        printer("  $field_name$_.clear();\n");
      } else {
        printer("  clear_$field_name$();\n");
      }
    }
    printer("}\n\n");
  }

  void CopyFrom(YBPrinter printer, Lightweight lightweight) const {
    printer("void $message_lw_name$::CopyFrom(const ");
    printer(lightweight ? "$message_lw_name$" : "$message_pb_name$");
    printer("& rhs) {\n");
    ScopedIndent copy_from_indent(printer);
    for (int j = 0; j != message_->field_count(); ++j) {
      const auto* field = message_->field(j);
      ScopedSubstituter field_substituter(printer, field);
      std::string source = lightweight ? "rhs.$field_name$_" : "rhs.$field_name$()";
      if (lightweight && StoreAsPointer(field)) {
        source = "*" + source;
      } else if (!lightweight && message_->options().map_entry()) {
        if (field->name() == "key") {
          source = "rhs.first";
        } else {
          source = "rhs.second";
        }
      }
      if (field->is_repeated()) {
        if (StoredAsSlice(field)) {
          printer(
              "$field_name$_.reserve(" + source + ".size());\n"
              "for (const auto& entry : " + source + ") {\n"
              "  $field_name$_.push_back(arena_.DupSlice(entry));\n"
              "}\n"
          );
        } else if (lightweight) {
          printer("$field_name$_ = " + source + ";\n");
        } else if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_ENUM) {
          printer(
              "$field_name$_.reserve(" + source + ".size());\n"
              "for (auto entry : " + source + ") {\n"
              "  $field_name$_.push_back(static_cast<$field_type$>(entry));\n"
              "}\n"
          );
        } else {
          printer("$field_name$_.assign(" + source + ".begin(), " + source + ".end());\n");
        }
      } else {
        bool check_has =
            (field->file()->syntax() == google::protobuf::FileDescriptor::Syntax::SYNTAX_PROTO2 ||
             lightweight) &&
            !message_->options().map_entry();
        if (StoreAsPointer(field)) {
          if (lightweight) {
            printer("if (rhs.$field_name$_) {\n");
          } else {
            printer("if (rhs.has_$field_name$()) {\n");
          }
          printer(
              "  mutable_$field_name$()->CopyFrom(" + source + ");\n"
          );
        } else {
          if (check_has) {
            printer("if (rhs.has_$field_name$()) {\n");
          } else {
            printer("{\n");
          }
          if (IsMessage(field)) {
            printer("  $field_name$_.CopyFrom(" + source + ");\n");
          } else if (StoredAsSlice(field)) {
            printer("  $field_name$_ = arena_.DupSlice(" + source + ");\n");
          } else {
            printer("  $field_name$_ = " + source + ";\n");
          }
          if (!lightweight) {
            printer("  has_fields_.Set($message_name$Fields::k$field_camelcase_name$);\n");
          }
        }
        if (check_has) {
          printer(
              "} else {\n"
              "  clear_$field_name$();\n"
          );
        }
        printer("}\n");
      }
    }
    if (need_has_fields_enum_ && lightweight) {
      printer("has_fields_ = rhs.has_fields_;\n");
    }
    copy_from_indent.Reset("}\n\n");
  }

  void Parse(YBPrinter printer) const {
    printer(
        "Status $message_lw_name$::ParseFromCodedStream("
            "google::protobuf::io::CodedInputStream* input) {\n"
    );

    ScopedIndent method_indent(printer);

    printer(
        "for (;;) {\n"
    );

    ScopedIndent loop_indent(printer);
    printer(
        "auto p = input->ReadTagWithCutoffNoLastTag($cutoff$);\n"
        "if (!p.second && !p.first) {\n"
        "  return Status::OK();\n"
        "}\n"
        "switch(::google::protobuf::internal::WireFormatLite::GetTagFieldNumber(p.first)) {\n"
    );

    ScopedIndent switch_indent(printer);
    for (int j = 0; j != message_->field_count(); ++j) {
      const auto* field = message_->field(j);
      ScopedSubstituter field_substituter(printer, field);

      printer(
          "case $field_number$: { // $field_name$\n"
      );
      ScopedIndent case_indent(printer);
      auto parse_failed = "  return ::yb::rpc::ParseFailed(\"$field_name$\");\n";
      if (field->is_repeated()) {
        if (IsMessage(field)) {
          printer(
              "if (!$field_serialization$::Read(input, &$field_name$_.emplace_back())) {\n"
          );
          printer(parse_failed);
          printer("}\n");
        } else {
          if (field->is_packable()) {
            auto packed_tag = GOOGLE_PROTOBUF_WIRE_FORMAT_MAKE_TAG(
                field->number(), WireFormatLite::WIRETYPE_LENGTH_DELIMITED);
            printer(
                "if (p.first == " + std::to_string(packed_tag) + ") {\n"
            );
            ScopedIndent if_packed_indent(printer);
            printer(
                "int length;\n"
                "if (!input->ReadVarintSizeAsInt(&length)) {\n"
            );
            printer(parse_failed);
            printer(
                "}\n"
                "auto old_limit = input->PushLimit(length);\n"
                "while (input->BytesUntilLimit() > 0) {\n"
            );
            ScopedIndent while_indent(printer);
            printer(
                "$field_name$_.emplace_back();\n"
                "if (!$field_serialization$::Read(input, &$field_name$_.back())) {\n"
            );
            printer(parse_failed);
            printer("}\n");
            while_indent.Reset("}\n\n");
            printer(
                "input->PopLimit(old_limit);\n"
            );
            if_packed_indent.Reset();
            printer("} else {\n");
            printer.printer().Indent();
          }
          printer(
              "$field_name$_.emplace_back();\n"
              "if (!$field_serialization$::Read(input, &$field_name$_.back())) {\n"
          );
          printer(parse_failed);
          printer("}\n");
          if (field->is_packable()) {
            printer.printer().Outdent();
            printer("}\n");
          }
        }
      } else {
        printer("if (!$field_serialization$::Read(input, mutable_$field_name$())) {\n");
        printer(parse_failed);
        printer(
            "}\n"
        );
      }
      printer(
          "break;\n"
      );
      case_indent.Reset("}\n");
    }

    switch_indent.Reset("}\n");
    loop_indent.Reset("}\n");
    method_indent.Reset("}\n\n");
  }

  void Size(YBPrinter printer) const {
    printer(
        "size_t $message_lw_name$::SerializedSize() const {\n"
    );

    ScopedIndent method_indent(printer);

    printer("size_t result = 0;\n");

    for (int j = 0; j != message_->field_count(); ++j) {
      auto* field = message_->field(j);
      ScopedSubstituter field_substituter(printer, field);
      if (!field->is_repeated()) {
        printer("if (has_$field_name$()) ");
      }
      printer("{\n");
      ScopedIndent field_indent(printer);
      auto tag_size = WireFormatLite::TagSize(field->number(), FieldType(field));
      auto fixed_size = FixedSize(field);
      if (fixed_size) {
        if (field->is_packed()) {
          printer(
              "size_t body_size = " + std::to_string(fixed_size) + " * $field_name$_.size();\n"
              "result += " + std::to_string(tag_size) +
              " + ::google::protobuf::io::CodedOutputStream::VarintSize32("
                  "narrow_cast<uint32_t>(body_size)) + body_size"
          );
        } else {
          printer("result += " + std::to_string(tag_size + fixed_size));
          if (field->is_repeated()) {
            printer(" * $field_name$_.size()");
          }
        }
        printer(";\n");
      } else {
        printer(
            "result += ::yb::rpc::$field_serialization_prefix$Size<$field_serialization$, " +
            std::to_string(tag_size) + ">("
        );
        if (StoreAsPointer(field)) {
          printer("*");
        }
        printer("$field_name$_");
        if (field->is_packed()) {
          printer(", &$field_name$_cached_size_");
        }
        printer(");\n");
      }
      field_indent.Reset("}\n");
    }


    printer(
        "cached_size_ = result;\n"
        "return result;\n"
    );
    method_indent.Reset("}\n\n");
  }

  void Serialize(YBPrinter printer) const {
    printer(
        "uint8_t* $message_lw_name$::SerializeToArray(uint8_t* out) const {\n");

    ScopedIndent method_indent(printer);

    for (int j = 0; j != message_->field_count(); ++j) {
      auto* field = message_->field(j);
      ScopedSubstituter field_substituter(printer, field);

      if (!field->is_repeated()) {
        printer("if (has_$field_name$()) {\n");
        printer.printer().Indent();
      }

      auto tag = GOOGLE_PROTOBUF_WIRE_FORMAT_MAKE_TAG(
          field->number(),
          field->is_packed() ? WireFormatLite::WIRETYPE_LENGTH_DELIMITED : WireType(field));
      printer(
          "out = ::yb::rpc::$field_serialization_prefix$Write<$field_serialization$, " +
          std::to_string(tag) + ">("
      );
      if (StoreAsPointer(field)) {
        printer("*");
      }
      printer("$field_name$_, ");
      if (field->is_packed()) {
        auto fixed_size = FixedSize(field);
        if (fixed_size) {
          printer(std::to_string(fixed_size) + " * $field_name$_.size(), ");
        } else {
          printer("$field_name$_cached_size_, ");
        }
      }
      printer("out);\n");
      if (!field->is_repeated()) {
        printer.printer().Outdent();
        printer("}\n");
      }
    }

    printer("return out;\n");
    method_indent.Reset("}\n\n");
  }

  void ToGoogleProtobuf(YBPrinter printer) const {
    printer(
      "void $message_lw_name$::ToGoogleProtobuf($message_pb_name$* out) const {\n"
    );

    ScopedIndent method_indent(printer);

    for (int j = 0; j != message_->field_count(); ++j) {
      const auto* field = message_->field(j);
      ScopedSubstituter field_substituter(printer, field);
      if (IsMessage(field) && !field->is_map()) {
        if (!field->is_repeated()) {
          printer("if (has_$field_name$()) {\n");
          printer.printer().Indent();
        }
        printer("$field_name$().ToGoogleProtobuf(out->mutable_$field_name$());\n");
        if (!field->is_repeated()) {
          printer.printer().Outdent();
          printer("}\n");
        }
      } else if (field->is_repeated()) {
        printer("{\n");
        ScopedIndent block_indent(printer);
        printer(
            "auto& repeated = *out->mutable_$field_name$();\n"
        );
        if (field->is_map()) {
          printer(
              "repeated.clear();\n"
              "for (const auto& entry : $field_name$_) {\n"
          );
          const auto* key_field = field->message_type()->FindFieldByName("key");
          const auto* value_field = field->message_type()->FindFieldByName("value");
          printer("  repeated[entry.key()");
          if (StoredAsSlice(key_field)) {
            printer(".ToBuffer()");
          }
          printer("] = entry.value()");
          if (StoredAsSlice(value_field)) {
            printer(".ToBuffer()");
          }
          printer(";\n");
        } else {
          printer(
              "repeated.Clear();\n"
              "repeated.Reserve(narrow_cast<int>($field_name$_.size()));\n"
              "for (const auto& entry : $field_name$_) {\n"
          );
          if (StoredAsSlice(field)) {
            printer("  repeated.Add()->assign(entry.cdata(), entry.size());\n");
          } else {
            printer("  repeated.Add(entry);\n");
          }
        }

        printer("}\n");
        block_indent.Reset("}\n");
      } else {
        printer("if (has_$field_name$()) {\n");

        if (StoredAsSlice(field)) {
          if (message_->options().map_entry()) {
            if (field->name() == "key") {
              printer("  out->first.assign(");
            } else {
              printer("  out->second.assign(");
            }
          } else {
            printer("  out->set_$field_name$(");
          }
          printer("$field_name$_.cdata(), $field_name$_.size());\n");
        } else {
          if (message_->options().map_entry()) {
            if (field->name() == "key") {
              printer("  out->first = $field_name$_;");
            } else {
              printer("  out->second = $field_name$_;");
            }
          } else {
            printer("  out->set_$field_name$($field_name$_);\n");
          }
        }

        if (!message_->options().map_entry()) {
          printer(
              "} else {\n"
              "  out->clear_$field_name$();\n"
          );
        }
        printer("}\n");
      }
    }

    method_indent.Reset("}\n\n");
  }

  bool StoreAsPointer(const google::protobuf::FieldDescriptor* field) const {
    return cycle_dependencies_.count(field) || IsPointerField(field);
  }

  const google::protobuf::Descriptor* message_;
  bool generated_ = false;
  bool need_has_fields_enum_ = false;
  std::unordered_set<const google::protobuf::FieldDescriptor*> cycle_dependencies_;
};

} // namespace

class MessagesGenerator::Impl {
 public:
  void Header(YBPrinter printer, const google::protobuf::FileDescriptor* file) {
    printer(
        "// THIS FILE IS AUTOGENERATED FROM $path$\n"
        "\n"
        "#ifndef $upper_case$_MESSAGES_H\n"
        "#define $upper_case$_MESSAGES_H\n"
        "\n"
    );

    bool generating_any = false;
    for (int i = 0; i != file->message_type_count(); ++i) {
      if (IsLwAny(file->message_type(i))) {
        generating_any = true;
        break;
      }
    }

    if (generating_any) {
      printer("#include <google/protobuf/any.pb.h>\n");
    }

    printer(
        "#include \"yb/rpc/lightweight_message.h\"\n"
        "#include \"yb/util/memory/arena_list.h\"\n"
        "#include \"yb/util/memory/mc_types.h\"\n"
        "#include \"$path_no_extension$.pb.h\"\n"
        "\n"
    );

    auto deps = ListDependencies(file);
    if (!deps.empty()) {
      for (const auto& dep : deps) {
        printer(
            "#include \"" + dep + ".messages.h\"\n"
        );
      }
      printer("\n");
    }

    printer(
        "$open_namespace$\n"
    );

    for (int i = 0; i != file->message_type_count(); ++i) {
      MessageForward(printer, file->message_type(i));
    }

    printer("\n");

    for (int i = 0; i != file->message_type_count(); ++i) {
      MessageDeclaration(printer, file->message_type(i));
    }

    printer(
        "$close_namespace$\n"
        "#endif // $upper_case$_MESSAGES_H\n"
    );
  }

  void Source(YBPrinter printer, const google::protobuf::FileDescriptor* file) {
    printer(
        "// THIS FILE IS AUTOGENERATED FROM $path$\n"
        "\n"
        "#include \"$path_no_extension$.messages.h\"\n"
        "\n"
        "#include \"yb/gutil/strings/numbers.h\"\n"
        "\n"
        "$open_namespace$\n"
    );

    for (int i = 0; i != file->message_type_count(); ++i) {
      MessageDefinition(printer, file->message_type(i));
    }

    printer("$close_namespace$");
  }

 private:
  void MessageForward(YBPrinter printer, const google::protobuf::Descriptor* message) {
    for (auto i = 0; i != message->nested_type_count(); ++i) {
      MessageForward(printer, message->nested_type(i));
    }

    ScopedSubstituter message_substituter(printer, message);
    printer("class $message_lw_name$;\n");
  }

  bool MessageDeclaration(YBPrinter printer, const google::protobuf::Descriptor* message) {
    auto insert_result = messages_.emplace(message, nullptr);
    if (insert_result.second) {
      insert_result.first->second = std::make_unique<Message>(message);
    } else {
      return insert_result.first->second->generated();
    }

    for (auto i = 0; i != message->nested_type_count(); ++i) {
      MessageDeclaration(printer, message->nested_type(i));
    }

    for (int j = 0; j != message->field_count(); ++j) {
      auto* field = message->field(j);
      if (!field->is_repeated() && field->message_type() &&
          field->message_type()->file() == message->file()) {
        if (!MessageDeclaration(printer, field->message_type())) {
          printer("// CYCLE " + message->name() + "\n");
          insert_result.first->second->CycleDependency(field);
        }
      }
    }

    insert_result.first->second->Declaration(printer);
    return true;
  }

  void MessageDefinition(YBPrinter printer, const google::protobuf::Descriptor* message) {
    for (int i = 0; i != message->nested_type_count(); ++i) {
      MessageDefinition(printer, message->nested_type(i));
    }

    messages_[message]->Definition(printer);
  }

  std::unordered_map<const google::protobuf::Descriptor*, std::unique_ptr<Message>> messages_;
};

MessagesGenerator::MessagesGenerator() : impl_(new Impl) {
}

MessagesGenerator::~MessagesGenerator() {
}

void MessagesGenerator::Header(YBPrinter printer, const google::protobuf::FileDescriptor* file) {
  impl_->Header(printer, file);
}

void MessagesGenerator::Source(YBPrinter printer, const google::protobuf::FileDescriptor* file) {
  impl_->Source(printer, file);
}

} // namespace gen_yrpc
} // namespace yb
