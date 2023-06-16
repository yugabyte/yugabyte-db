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

#include "yb/gutil/stl_util.h"

using google::protobuf::internal::WireFormatLite;
using namespace std::literals;

namespace yb {
namespace gen_yrpc {

namespace {

YB_STRONGLY_TYPED_BOOL(OneOfOnly);

const std::string kFieldCase =
    "$message_name$::$field_containing_oneof_cap_name$Case::k$field_camelcase_name$";

std::string SetHasField(const google::protobuf::FieldDescriptor* field) {
  return field->containing_oneof()
      ? "$field_containing_oneof_name$_case_ = " + kFieldCase + ";"
      : "has_fields_.Set($message_name$Fields::k$field_camelcase_name$);";
}

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
      if (field->is_repeated() || StoreAsPointer(field) || field->containing_oneof()) {
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
        "explicit $message_lw_name$(::yb::ThreadSafeArena* arena);\n"
        "$message_lw_name$(::yb::ThreadSafeArena* arena, const $message_lw_name$& rhs);\n"
        "\n"
        "$message_lw_name$(::yb::ThreadSafeArena* arena, const $message_pb_name$& rhs) \n"
        "    : $message_lw_name$(arena) {\n"
        "  CopyFrom(rhs);\n"
        "}\n"
        "\n"
        "~$message_lw_name$() {}\n"
        "\n"
        "void operator=(const $message_lw_name$& rhs) {\n"
        "  CopyFrom(rhs);\n"
        "}\n"
        "\n"
        "void operator=(const $message_pb_name$& rhs) {\n"
        "  CopyFrom(rhs);\n"
        "}\n"
        "\n"
        "Status ParseFromCodedStream("
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
              RelativeClassPath(UnnestedName(nested, Lightweight::kTrue, FullPath::kTrue),
                                message_->full_name()) + ";\n");
      has_nested_using = true;
    }

    if (has_nested_using) {
      printer("\n");
    }

    const google::protobuf::OneofDescriptor* last_oneof = nullptr;
    for (int j = 0; j != message_->field_count(); ++j) {
      const auto* field = message_->field(j);
      ScopedSubstituter field_substituter(printer, field);

      if (last_oneof != field->containing_oneof()) {
        last_oneof = field->containing_oneof();
        if (last_oneof) {
          printer(
              "$message_name$::$field_containing_oneof_cap_name$Case "
                  "$field_containing_oneof_name$_case() const {\n"
              "  return $field_containing_oneof_name$_case_;\n"
              "}\n"
              "\n"
          );
        }
      }

      std::string set_has_field = SetHasField(field);

      if (IsSimple(field)) {
        printer(
            "$field_stored_type$ $field_name$() const {\n"
            "  return $field_accessor$;\n"
            "}\n"
            "\n");
        if (StoredAsSlice(field)) {
          printer(
              "void dup_$field_name$($field_stored_type$ value) {\n"
              "  " + set_has_field + "\n"
              "  $field_accessor$ = arena_.DupSlice(value);\n"
              "}\n\n"
              "void ref_$field_name$($field_stored_type$ value) {\n"
              "  " + set_has_field + "\n"
              "  $field_accessor$ = value;\n"
              "}\n\n"
          );
        } else {
          printer(
              "void set_$field_name$($field_stored_type$ value) {\n"
              "  " + set_has_field + "\n"
              "  $field_accessor$ = value;\n"
              "}\n\n"
          );
        }
      } else if (StoreAsPointer(field)) {
        printer(
            "const $field_stored_type$& $field_name$() const {\n"
            "  return ");
        if (field->containing_oneof()) {
          printer("$field_containing_oneof_name$_case_ == " + kFieldCase);
        } else {
          printer("$field_accessor$");
        }
        printer(
            " ? *$field_accessor$ "
                ": ::yb::rpc::empty_message<$field_stored_type$>();\n"
            "}\n\n"
            "$field_stored_type$& ref_$field_name$($field_stored_type$* value) {\n"
        );
        if (field->containing_oneof()) {
          printer("  " + set_has_field + "\n");
        }
        printer(
            "  $field_accessor$ = value;\n"
            "  return *$field_accessor$;\n"
            "}\n\n"
        );
      } else {
        printer(
            "const $field_stored_type$& $field_name$() const {\n"
            "  return $field_accessor$;\n"
            "}\n\n"
        );
      }

      printer(
          "$field_stored_type$* mutable_$field_name$() {\n"
      );

      ScopedIndent mutable_ident(printer);

      if (StoreAsPointer(field)) {
        if (field->containing_oneof()) {
          printer("if (!has_$field_name$()) {\n"
                  "  " + set_has_field + "\n");
        } else {
          printer("if (!$field_name$_) {\n");
        }
        printer(
          "  $field_accessor$ = arena_.NewObject<$field_stored_type$>(&arena_);\n"
          "}\n"
          "return $field_accessor$;\n"
        );
      } else {
        if (!field->is_repeated()) {
          printer(set_has_field + "\n");
        }

        printer(
            "return &$field_accessor$;\n"
        );
      }

      mutable_ident.Reset("}\n\n");

      if (!field->is_repeated() || IsPointerField(field)) {
        printer("bool has_$field_name$() const {\n");
        if (field->containing_oneof()) {
          printer(
              "  return $field_containing_oneof_name$_case_ == "
              "$message_name$::$field_containing_oneof_cap_name$Case::k$field_camelcase_name$;\n");
        } else if (StoreAsPointer(field)) {
          printer("  return $field_name$_ != nullptr;\n");
        } else {
          printer("  return has_fields_.Test($message_name$Fields::k$field_camelcase_name$);\n");
        }
        printer("}\n\n");
        printer("void clear_$field_name$() {\n");
        if (field->containing_oneof()) {
          printer(
            "  if (!has_$field_name$()) {\n"
            "    return;\n"
            "  }\n"
            "  $field_containing_oneof_name$_case_ = "
                "$message_name$::$field_containing_oneof_cap_name$Case();\n"
          );
        } else if (StoreAsPointer(field)) {
          printer("  $field_name$_ = nullptr;\n");
        } else {
          if (IsMessage(field)) {
            printer("  $field_name$_.Clear();\n");
          } else {
            printer("  $field_name$_ = $field_default_value$;\n");
          }
          printer("  has_fields_.Reset($message_name$Fields::k$field_camelcase_name$);\n");
        }
        printer("}\n\n");
      }
      if (field->is_repeated()) {
        printer("size_t $field_name$_size() const {\n"
                "  return "
        );
        printer(IsPointerField(field) ? "$field_name$()" : "$field_accessor$");
        printer(".size();\n"
                "}\n\n"
        );
        if (IsMessage(field)) {
          printer(
              "$field_type$* add_$field_name$() {\n"
              "  return &");
          printer(IsPointerField(field) ? "mutable_$field_name$()->" : "$field_accessor$.");
          printer("emplace_back();\n"
                  "}\n\n"
          );
        } else if (StoredAsSlice(field)) {
          printer(
              "void add_dup_$field_name$(const ::yb::Slice& value) {\n"
              "  $field_accessor$.push_back(arena_.DupSlice(value));\n"
              "}\n\n"
              "void add_ref_$field_name$(const ::yb::Slice& value) {\n"
              "  $field_accessor$.push_back(value);\n"
              "}\n\n"
          );
        } else {
          printer(
              "void add_$field_name$($field_type$ value) {\n"
              "  $field_accessor$.push_back(value);\n"
              "}\n\n"
          );
        }
        if (!StoreAsPointer(field)) {
          printer(
              "void clear_$field_name$() {\n"
              "  $field_accessor$.clear();\n"
              "}\n\n"
          );
        }
      }
    }

    if (NeedArena(message_)) {
      printer(
          "::yb::ThreadSafeArena& arena() const {\n"
          "  return arena_;\n"
          "}\n\n"
      );
    }

    printer(
        "size_t cached_size() const {\n"
        "  return cached_size_.load(std::memory_order_relaxed);\n"
        "}\n\n"
    );

    public_scope.Reset();

    printer(" private:\n");

    ScopedIndent private_scope(printer);

    if (NeedArena(message_)) {
      printer(
          "::yb::ThreadSafeArena& arena_;\n"
      );
    }
    if (need_has_fields_enum_) {
      printer(
          "::yb::EnumBitSet<$message_name$Fields> has_fields_;\n"
      );
    }
    printer(
        "mutable std::atomic<size_t> cached_size_{0};\n"
    );

    for (int j = 0; j != message_->field_count(); ++j) {
      const auto* field = message_->field(j);
      if (!StoredAsSlice(field) || !field->containing_oneof()) {
        continue;
      }
      ScopedSubstituter field_substituter(printer, field);
      printer(
          "$field_stored_type$& direct_$field_name$() {\n"
          "  return reinterpret_cast<$field_stored_type$&>("
              "$field_containing_oneof_name$_.$field_name$_);\n"
          "}\n"
          "\n"
          "const $field_stored_type$& direct_$field_name$() const {\n"
          "  return reinterpret_cast<const $field_stored_type$&>("
               "$field_containing_oneof_name$_.$field_name$_);\n"
          "}\n"
          "\n"
      );
    }

    const google::protobuf::OneofDescriptor* current_oneof = nullptr;
    for (int j = 0; j != message_->field_count();) {
      const auto* field = message_->field(j);
      ScopedSubstituter field_substituter(printer, field);
      if (field->containing_oneof() != current_oneof) {
        current_oneof = field->containing_oneof();
        if (current_oneof) {
          printer("union {\n");
          printer.Indent();
        }
      }

      if (StoreAsPointer(field)) {
        printer("$field_stored_type$* $field_name$_");
        if (!current_oneof) {
          printer(" = nullptr");
        }
        printer(";\n");
      } else if (StoredAsSlice(field) && field->containing_oneof()) {
        printer("alignas($field_stored_type$) char $field_name$_[sizeof($field_stored_type$)];\n");
      } else if (IsSimple(field)) {
        printer("$field_stored_type$ $field_name$_");
        if (!current_oneof) {
          printer(" = $field_default_value$");
        }
        printer(";\n");
      } else {
        printer("$field_stored_type$ $field_name$_;\n");
        if (field->is_packed() && !FixedSize(field)) {
          printer(
              "mutable size_t $field_name$_cached_size_ = 0;\n"
          );
        }
      }
      ++j;
      if (j == message_->field_count() || message_->field(j)->containing_oneof() != current_oneof) {
        FinishOneOf(printer, current_oneof);
      }
    }

    private_scope.Reset("};\n\n");

    generated_ = true;
  }

  void FinishOneOf(YBPrinter printer, const google::protobuf::OneofDescriptor* current_oneof) {
    if (!current_oneof) {
      return;
    }

    printer.Outdent();
    printer("} " + current_oneof->name() + "_;\n");
    printer("$message_name$::$field_containing_oneof_cap_name$Case "
            "$field_containing_oneof_name$_case_ = "
            "$message_name$::$field_containing_oneof_cap_name$Case();\n");
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
    printer("$message_lw_name$::$message_lw_name$(::yb::ThreadSafeArena* arena)");
    bool first = true;
    if (NeedArena(message_)) {
      NextCtorField(printer, &first);
      printer("arena_(*arena)");
    }
    for (int j = 0; j != message_->field_count(); ++j) {
      const auto* field = message_->field(j);
      if ((!field->is_repeated() && !IsMessage(field)) || StoreAsPointer(field)) {
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
        "$message_lw_name$::$message_lw_name$("
            "::yb::ThreadSafeArena* arena, const $message_lw_name$& rhs)"
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
      const auto* field = message_->field(j);
      if (field->containing_oneof()) {
        continue;
      }
      NextCtorField(printer, &first);
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
    CopyFields(printer, Lightweight::kTrue, OneOfOnly::kTrue);
    body_indent.Reset("}\n\n");
  }

  void AppendToDebugString(YBPrinter printer) const {
    printer("void $message_lw_name$::AppendToDebugString(std::string* out) const {\n");
    ScopedIndent indent(printer);
    if (message_->field_count()) {
      printer("bool first = true;\n");
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
        if (IsPointerField(field)) {
          printer("if ($field_accessor$) {\n"
                  "  for (const auto& entry : *$field_accessor$) {\n");
          printer.Indent();
        } else {
          printer("for (const auto& entry : $field_accessor$) {\n");
        }
      } else {
        printer("if (has_$field_name$()) {\n");
      }
      ScopedIndent if_indent(printer);
      if (IsMessage(field)) {
        printer(
            "::yb::rpc::AppendFieldTitle(\"$field_name$\", \" { \", &first, out);\n"
            "size_t len = out->length();\n"
            "$field_value$.AppendToDebugString(out);\n"
            "*out += len != out->length() ? \" }\" : \"}\";\n"
        );
      } else if (StoredAsSlice(field)) {
        printer("::yb::rpc::AppendFieldTitle(\"$field_name$\", \": \\\"\", &first, out);\n");
        if (field->type() == google::protobuf::FieldDescriptor::TYPE_BYTES) {
          printer("*out += $field_value$.ToDebugString();\n");
        } else {
          printer("*out += $field_value$.ToBuffer();\n");
        }
        printer("*out += '\"';\n");
      } else {
        printer("::yb::rpc::AppendFieldTitle(\"$field_name$\", \": \", &first, out);\n");
        if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE) {
          printer("*out += ::SimpleDtoa($field_value$);\n");
        } else if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_FLOAT) {
          printer("*out += ::SimpleFtoa($field_value$);\n");
        } else if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_ENUM) {
          printer("*out += $nonlw_field_type$_Name($field_value$);\n");
        } else {
          printer("*out += std::to_string($field_value$);\n");
        }
      }
      if (field->is_repeated() && IsPointerField(field)) {
        printer.Outdent();
        printer("}\n");
      }
      if_indent.Reset("}\n");
    }
    indent.Reset("}\n\n");
  }

  void Clear(YBPrinter printer) const {
    printer("void $message_lw_name$::Clear() {\n");
    for (int j = 0; j != message_->field_count(); ++j) {
      const auto* field = message_->field(j);
      ScopedSubstituter field_substituter(printer, field);
      if (field->is_repeated() && !IsPointerField(field)) {
        printer("  $field_name$_.clear();\n");
      } else {
        printer("  clear_$field_name$();\n");
      }
    }
    printer("}\n\n");
  }

  void CopyFields(YBPrinter printer, Lightweight lightweight, OneOfOnly oneof_only) const {
    for (int j = 0; j != message_->field_count(); ++j) {
      const auto* field = message_->field(j);
      if (oneof_only && !field->containing_oneof()) {
        continue;
      }
      ScopedSubstituter field_substituter(printer, field);

      auto source = lightweight && (!StoredAsSlice(field) || !field->containing_oneof())
          ? "rhs.$field_accessor$"s : "rhs.$field_name$()"s;
      if (!lightweight && message_->options().map_entry()) {
        if (field->name() == "key") {
          source = "rhs.first";
        } else {
          source = "rhs.second";
        }
      }
      if (field->is_repeated()) {
        if (StoredAsSlice(field)) {
          printer(
              "$field_accessor$.clear();\n"
              "$field_accessor$.reserve(" + source + ".size());\n"
              "for (const auto& entry : " + source + ") {\n"
              "  $field_accessor$.push_back(arena_.DupSlice(entry));\n"
              "}\n"
          );
        } else if (lightweight) {
          if (StoreAsPointer(field)) {
            printer(
                "if (" + source + ") {\n"
                "  *mutable_$field_name$() = *" + source + ";\n"
                "} else {\n"
                "  clear_$field_name$();\n"
                "}\n");
          } else {
            printer("$field_accessor$ = " + source + ";\n");
          }
        } else if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_ENUM) {
          printer(
              "$field_accessor$.clear();\n"
              "$field_accessor$.reserve(" + source + ".size());\n"
              "for (auto entry : " + source + ") {\n"
              "  $field_accessor$.push_back(static_cast<$field_type$>(entry));\n"
              "}\n"
          );
        } else {
          if (StoreAsPointer(field)) {
            printer("mutable_$field_name$()->");
          } else {
            printer("$field_accessor$.");
          }
          printer("assign(" + source + ".begin(), " + source + ".end());\n");
        }
      } else {
        bool check_has =
            (field->file()->syntax() == google::protobuf::FileDescriptor::Syntax::SYNTAX_PROTO2 ||
             lightweight || IsMessage(field)) &&
            !message_->options().map_entry();
        if (StoreAsPointer(field)) {
          printer(
              "if (rhs.has_$field_name$()) {\n"
              "  mutable_$field_name$()->CopyFrom("s + (lightweight ? "*" : "") + source + ");\n"
          );
          if (field->containing_oneof()) {
              printer("  " + SetHasField(field) + "\n");
          }
        } else {
          if (check_has) {
            printer("if (rhs.has_$field_name$()) {\n");
          } else {
            printer("{\n");
          }
          if (IsMessage(field)) {
            printer("  $field_name$_.CopyFrom(" + source + ");\n");
          } else if (StoredAsSlice(field)) {
            printer("  $field_accessor$ = arena_.DupSlice(" + source + ");\n");
          } else {
            printer("  $field_accessor$ = " + source + ";\n");
          }
          if (!lightweight || field->containing_oneof()) {
            printer("  " + SetHasField(field) + "\n");
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
  }

  void CopyFrom(YBPrinter printer, Lightweight lightweight) const {
    printer("void $message_lw_name$::CopyFrom(const ");
    printer(lightweight ? "$message_lw_name$" : "$message_pb_name$");
    printer("& rhs) {\n");
    ScopedIndent copy_from_indent(printer);
    CopyFields(printer, lightweight, OneOfOnly::kFalse);
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
          printer("if (!$field_serialization$::Read(input, &");
          if (IsPointerField(field)) {
            printer("mutable_$field_name$()->");
          } else {
            printer("$field_accessor$.");
          }
          printer("emplace_back())) {\n");
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
                "$field_accessor$.emplace_back();\n"
                "if (!$field_serialization$::Read(input, &$field_accessor$.back())) {\n"
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
              "$field_accessor$.emplace_back();\n"
              "if (!$field_serialization$::Read(input, &$field_accessor$.back())) {\n"
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

    printer(
        "default: { // skip unknown fields\n"
    );
    ScopedIndent case_indent(printer);
    printer("::google::protobuf::internal::WireFormatLite::SkipField(input, p.first);\n");
    case_indent.Reset("}\n");

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
      } else if (IsPointerField(field)) {
        printer("if ($field_accessor$) ");
      }
      printer("{\n");
      ScopedIndent field_indent(printer);
      auto tag_size = WireFormatLite::TagSize(field->number(), FieldType(field));
      auto fixed_size = FixedSize(field);
      if (fixed_size) {
        if (field->is_packed()) {
          printer(
              "size_t body_size = " + std::to_string(fixed_size) + " * $field_accessor$.size();\n"
              "result += " + std::to_string(tag_size) +
              " + ::google::protobuf::io::CodedOutputStream::VarintSize32("
                  "narrow_cast<uint32_t>(body_size)) + body_size"
          );
        } else {
          printer("result += " + std::to_string(tag_size + fixed_size));
          if (field->is_repeated()) {
            printer(" * $field_accessor$.size()");
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
        printer("$field_accessor$");
        if (field->is_packed()) {
          printer(", &$field_name$_cached_size_");
        }
        printer(");\n");
      }
      field_indent.Reset("}\n");
    }


    printer(
        "cached_size_.store(result, std::memory_order_relaxed);\n"
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

      bool has_indent = false;
      if (!field->is_repeated()) {
        printer("if (has_$field_name$()) {\n");
        has_indent = true;
      } else if (IsPointerField(field)) {
        printer("if ($field_accessor$) {\n");
        has_indent = true;
      }
      if (has_indent) {
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
      printer("$field_accessor$, ");
      if (field->is_packed()) {
        auto fixed_size = FixedSize(field);
        if (fixed_size) {
          printer(std::to_string(fixed_size) + " * $field_accessor$.size(), ");
        } else {
          printer("$field_name$_cached_size_, ");
        }
      }
      printer("out);\n");
      if (has_indent) {
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
              "for (const auto& entry : $field_accessor$) {\n"
          );
          const auto* key_field = field->message_type()->FindFieldByName("key");
          const auto* value_field = field->message_type()->FindFieldByName("value");
          auto dest = "repeated[entry.key()"s;
          if (StoredAsSlice(key_field)) {
            dest += ".ToBuffer()";
          }
          dest += "]";
          std::string src = "entry.value()";
          if (StoredAsSlice(value_field)) {
            src += ".ToBuffer()";
          }
          printer("  ");
          printer(Format(
              IsMessage(value_field) ? "$1.ToGoogleProtobuf(&$0)" : "$0 = $1", dest, src));
          printer(";\n");
        } else {
          printer(
              "repeated.Clear();\n"
              "repeated.Reserve(narrow_cast<int>($field_accessor$.size()));\n"
              "for (const auto& entry : $field_accessor$) {\n"
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
          printer("$field_accessor$.cdata(), $field_accessor$.size());\n");
        } else {
          if (message_->options().map_entry()) {
            if (field->name() == "key") {
              printer("  out->first = $field_name$_;");
            } else {
              printer("  out->second = $field_name$_;");
            }
          } else {
            printer("  out->set_$field_name$($field_accessor$);\n");
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
    return cycle_dependencies_.count(field) || IsPointerField(field) ||
           (field->containing_oneof() && field->message_type());
  }

  const google::protobuf::Descriptor* message_;
  bool generated_ = false;
  bool need_has_fields_enum_ = false;
  std::unordered_set<const google::protobuf::FieldDescriptor*> cycle_dependencies_;
};

std::vector<const google::protobuf::EnumValueDescriptor*> EnumValues(
    const google::protobuf::EnumDescriptor* enum_desc) {
  std::vector<const google::protobuf::EnumValueDescriptor*> values;
  for (int i = 0; i != enum_desc->value_count(); ++i) {
    values.push_back(enum_desc->value(i));
  }
  std::sort(values.begin(), values.end(), [](const auto* lhs, const auto* rhs) {
    return lhs->number() < rhs->number() ||
           (lhs->number() == rhs->number() && lhs->index() < rhs->index());
  });
  Unique(&values, [](const auto* lhs, const auto* rhs) {
    return lhs->number() == rhs->number();
  });
  return values;
}

} // namespace

class MessagesGenerator::Impl {
 public:
  void Header(YBPrinter printer, const google::protobuf::FileDescriptor* file) {
    printer(
        "// THIS FILE IS AUTOGENERATED FROM $path$\n"
        "\n"
        "#pragma once\n"
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
      printer("#include <google/protobuf/any.pb.h>\n\n");
    }

    printer("#include \"yb/rpc/lightweight_message.h\"\n\n");

    if (file->enum_type_count()) {
      printer("#include \"yb/util/enums.h\"\n");
    }
    printer(
        "#include \"yb/util/memory/arena.h\"\n"
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

    for (int i = 0; i != file->enum_type_count(); ++i) {
      EnumDeclaration(printer, file->enum_type(i));
    }

    for (int i = 0; i != file->message_type_count(); ++i) {
      MessageForward(printer, file->message_type(i));
    }

    printer("\n");

    for (int i = 0; i != file->message_type_count(); ++i) {
      ToLightweightMessage(printer, file->message_type(i));
    }

    printer("\n");

    for (int i = 0; i != file->message_type_count(); ++i) {
      MessageDeclaration(printer, file->message_type(i));
    }

    printer(
        "$close_namespace$"
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

    for (int i = 0; i != file->enum_type_count(); ++i) {
      EnumDefinition(printer, file->enum_type(i));
    }

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

  void ToLightweightMessage(YBPrinter printer, const google::protobuf::Descriptor* message) {
    for (auto i = 0; i != message->nested_type_count(); ++i) {
      ToLightweightMessage(printer, message->nested_type(i));
    }

    ScopedSubstituter message_substituter(printer, message);
    printer("$message_lw_name$* LightweightMessageType($message_name$*);\n");
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
      auto* ref_type = field->message_type();
      if (ref_type == message) {
        if (!field->is_repeated()) {
          insert_result.first->second->CycleDependency(field);
        }
        continue;
      }
      if (!field->is_repeated() && ref_type && ref_type->file() == message->file()) {
        if (!MessageDeclaration(printer, ref_type)) {
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

  void EnumDeclaration(YBPrinter printer, const google::protobuf::EnumDescriptor* enum_desc) {
    auto name = LightweightName(enum_desc);
    if (!name) {
      return;
    }

    ScopedSubstituter enum_substituter(printer, enum_desc);

    printer("YB_DEFINE_ENUM($enum_lw_name$,\n");
    {
      ScopedIndent enum_scope(printer);
      for (const auto* value : EnumValues(enum_desc)) {
        printer(Format("($0) // $1\n", value->name(), value->number()));
      }
    }
    printer(");\n\n");
    printer("$enum_name$ ToPB($enum_lw_name$ value);\n");
    printer("$enum_lw_name$ ToLW($enum_name$ value);\n\n");
  }

  void EnumDefinition(YBPrinter printer, const google::protobuf::EnumDescriptor* enum_desc) {
    auto name = LightweightName(enum_desc);
    if (!name) {
      return;
    }

    ScopedSubstituter enum_substituter(printer, enum_desc);
    auto values = EnumValues(enum_desc);
    GenEnumConverter(printer, enum_desc, "ToPB", "enum_lw_name", "enum_name");
    GenEnumConverter(printer, enum_desc, "ToLW", "enum_name", "enum_lw_name");
  }

  void GenEnumConverter(
      YBPrinter printer, const google::protobuf::EnumDescriptor* enum_desc,
      const std::string& name, const std::string& from_type, const std::string& to_type) {
    printer(Format("$$$2$$ $0($$$1$$ value) {\n", name, from_type, to_type));
    {
      ScopedIndent function_scope(printer);
      printer("switch (value) {\n");
      for (const auto* value : EnumValues(enum_desc)) {
        printer(Format("case $$$0$$::$1:\n", from_type, value->name()));
        printer(Format("  return $$$0$$::$1;\n", to_type, value->name()));
      }
      if (enum_desc->file()->syntax() == google::protobuf::FileDescriptor::SYNTAX_PROTO3 &&
          name == "ToLW") {
        printer(Format("case $$$0$$::$$$0$$_INT_MIN_SENTINEL_DO_NOT_USE_:\n", from_type));
        printer(Format("case $$$0$$::$$$0$$_INT_MAX_SENTINEL_DO_NOT_USE_:\n", from_type));
        printer("  break;\n");
      }
      printer("};\n");
      printer(Format("FATAL_INVALID_ENUM_VALUE($$$0$$, value);\n", from_type));
    }
    printer("}\n\n");
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
