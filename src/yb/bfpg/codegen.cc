//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------
#include <fstream>
#include <vector>

#include "yb/util/logging.h"

#include "yb/bfpg/directory.h"
#include "yb/common/ql_type.h"
#include "yb/gutil/strings/substitute.h"

using std::endl;
using std::map;
using std::ofstream;
using std::string;
using std::vector;

namespace yb {
namespace bfpg {

static const char *kFileStart =
  "// Copyright (c) YugaByte, Inc.\n\n";

static const char *kFileNamespace =
  "namespace yb {\n"
  "namespace bfpg {\n"
  "\n";

static const char *kFileEnd =
  "\n"
  "} // namespace bfpg\n"
  "} // namespace yb\n";

struct BFClassInfo {
  BFClassInfo(const string& cname, const string& oname, const string& ovl_oname)
      : class_name(cname), opname(oname), overloaded_opname(ovl_oname) {
  }

  string class_name;
  string opname;
  string overloaded_opname;
};

class BFCodegen {
 public:
  static const int kHasParamResult = 0;
  static const int kHasParamOnly = 1;
  static const int kHasResultOnly = 1;

  // Because SQL functions might need to support IN/OUT parameters, we won't provide const& option.
  // Each argument is either a shared_ptr, raw pointer, or reference.
  enum class BFApiParamOption {
    kSharedPtr, // Both arguments and returned-result are shared_ptr.
    kRawPtr,    // Both arguments and returned-result are raw pointers.
    kRefAndRaw, // Arguments are references (Type &arg), and returned-result is a raw pointer.
  };

  // Typeargs datatype.
  void GenerateOpcodes(string build_dir) {
    ofstream fopcode;
    fopcode.open(build_dir + "/gen_opcodes.h");
    fopcode << kFileStart
            << "#ifndef YB_UTIL_BFPG_GEN_OPCODES_H_" << endl
            << "#define YB_UTIL_BFPG_GEN_OPCODES_H_" << endl
            << endl
            << "#include <unordered_map>" << endl
            << "#include <string>" << endl
            << endl
            << kFileNamespace;

    // Start an enum class with a NO_OP.
    operator_ids_.reserve(kBFDirectory.size());
    fopcode << "enum class BFOpcode : int32_t {" << endl;

    // All builtin operators should be prefix with "OP_".
    string min_opcode;
    int op_index = 0;
    for (BFDecl entry : kBFDirectory) {
      // Form the opcode and print it.
      string current_opcode = strings::Substitute("OPCODE_$0_$1", entry.cpp_name(), op_index);
      fopcode << "  " << current_opcode << "," << endl;
      if (op_index == 0) {
        min_opcode = current_opcode;
      }

      // Find the last generated opcode that this opcode is overloading.
      string overloaded_opcode;
      if (yql2opcode_.find(entry.ql_name()) == yql2opcode_.end()) {
        overloaded_opcode = current_opcode;
      } else {
        overloaded_opcode = yql2opcode_[entry.ql_name()];
      }
      yql2opcode_[entry.ql_name()] = current_opcode;

      // Use opcode enum value to create unique operator name. This operator keeps the last
      // overloaded opcode to form a chain between overloading opcodes for the same ql_name.
      // Using this chain we can track all opcodes that are mapped to the same ql_name.
      operator_ids_.emplace_back(strings::Substitute("OPERATOR_$0_$1", entry.cpp_name(), op_index),
                                 current_opcode, overloaded_opcode);
      op_index++;
    }
    fopcode << "  OPCODE_MAX_VALUE" << endl;
    fopcode << "};" << endl;

    fopcode << "const BFOpcode BFOPCODE_NOOP = BFOpcode::" << min_opcode << ";" << endl
            << "const BFOpcode BFOPCODE_MIN_VALUE = BFOpcode::" << min_opcode << ";" << endl
            << "const BFOpcode BFOPCODE_MAX_VALUE = BFOpcode::OPCODE_MAX_VALUE" << ";" << endl
            << endl;

    fopcode << "extern const std::unordered_map<std::string, BFOpcode> kBfPgsqlName2Opcode;"
            << endl;

    // Ending the file.
    fopcode << kFileEnd;
    fopcode << "#endif" << endl;
    fopcode.close();
  }

  void GenerateOpcodeTable(string build_dir) {
    ofstream fopcode;
    fopcode.open(build_dir + "/gen_opcode_table.cc");

    fopcode << kFileStart
            // Including header files.
            << "#include <iostream>" << endl
            << "#include <unordered_map>" << endl
            << "#include <string>" << endl
            << "#include \"yb/bfpg/gen_opcodes.h\"" << endl
            << endl
            // Use namespaces.
            << "using std::string;" << endl
            << "using std::unordered_map;" << endl
            << endl
            << kFileNamespace;

    // Generating code.
    fopcode << "// Defining table to map ql_name to opcodes." << endl;
    fopcode << "const std::unordered_map<string, BFOpcode> kBfPgsqlName2Opcode = {" << endl;
    for (auto entry : yql2opcode_) {
      // For overload function only the opcode with max value is inserted.
      // string ql_name = entry.first;
      // string opname = strings::Substitute("BFOpcode::OPCODE_$0_$1", entry.first, entry.second);
      // string opname = entry.second;
      fopcode << "  { \"" << entry.first << "\", " << "BFOpcode::" << entry.second << " }," << endl;
    }
    fopcode << "};" << endl;

    // Ending the file.
    fopcode << kFileEnd;
    fopcode.close();
  }

  void GenerateOperators(string build_dir) {
    // Create header file, "gen_operator.h", for operator declarations.
    ofstream foper_h;
    foper_h.open(build_dir + "/gen_operator.h");
    foper_h << kFileStart
            << "#ifndef YB_UTIL_BFPG_GEN_OPERATOR_H_" << endl
            << "#define YB_UTIL_BFPG_GEN_OPERATOR_H_" << endl
            << endl
            << "#include <vector>" << endl
            << "#include <string>" << endl
            << endl
            << "#include \"yb/bfpg/base_operator.h\"" << endl
            << "#include \"yb/bfpg/bfunc.h\"" << endl
            << "#include \"yb/bfpg/bfunc_convert.h\"" << endl
            << "#include \"yb/bfpg/bfunc_standard.h\"" << endl
            << "#include \"yb/util/status.h\"" << endl
            << endl
            // Use namespaces.
            << "using std::vector;" << endl
            << endl
            << kFileNamespace;

    int op_index = 0;
    for (BFDecl entry : kBFDirectory) {
      // Define operator class.
      GenerateDecl(entry, foper_h, operator_ids_[op_index].class_name);
      op_index++;
    }
    foper_h << endl;

    foper_h << "extern const std::vector<BFOperator::SharedPtr> kBFOperators;" << endl
            << endl;

    // Ending the header file.
    foper_h << kFileEnd
            << "#endif" << endl;
    foper_h.close();
  }

  void GenerateDecl(BFDecl entry, ofstream &foper_h, string class_name) {
    // Declare an operator with the following specification
    // class OPERATOR_xxx : public BFOperator {
    //  public:
    //   OPERATOR_xxx(...) : BFOperator(...);
    //   static Status Exec(...); -- This takes mutable parameters.
    // };
    foper_h << "class " << class_name << " : public BFOperator {" << endl
            << " public:" << endl
            << "  " << class_name << "(" << endl
            << "    BFOpcode opcode," << endl
            << "    BFOpcode overloaded_opcode," << endl
            << "    const BFDecl *op_decl)" << endl
            << "      : BFOperator(opcode, overloaded_opcode, op_decl) {" << endl
            << "  }" << endl
            << endl;

    GenerateExecFunc(entry, foper_h, BFApiParamOption::kRefAndRaw);

    // End operator class.
    foper_h << "};" << endl
            << endl;
  }

  void GenerateExecFunc(BFDecl entry, ofstream &foper_h, BFApiParamOption param_option) {
    // Print function call. Four possible cases
    // - No parameter & no result:     func()
    //
    // - No result:                    func(params[0])
    //                              OR func(&params[0])
    //
    // - No parameter:                 func(result)
    //
    // - Has parameter & result:       func(params[0], params[1], result)
    //                              OR func(&params[0], &params[1], result)

    // When arguments are ref, character "&" is used to convert it to pointer. For argument that
    // are already pointers, no conversion is needed.
    const char *param_pointer = "params";
    bool pass_result = true;

    switch (param_option) {
      case BFApiParamOption::kSharedPtr:
        // For shared_ptr, the parameter would be "const std::shared_ptr<>&".
        foper_h << "  template<typename PType, typename RType>" << endl
                << "  static Status Exec(const std::vector<std::shared_ptr<PType>>& params," << endl
                << "                     const std::shared_ptr<RType>& result) {" << endl
                << "    return "
                << entry.cpp_name() << "(";
        break;
      case BFApiParamOption::kRawPtr:
        // Raw pointer.
        foper_h << "  template<typename PType, typename RType>" << endl
                << "  static Status ExecRaw(const std::vector<PType*>& params," << endl
                << "                        RType *result) {" << endl
                << "    return " << entry.cpp_name() << "(";
        break;
      case BFApiParamOption::kRefAndRaw:
        // Reference of object.
        foper_h << "  static Result<BFRetValue> ExecRefAndRaw(const BFParams& params) {" << endl;

        if (entry.param_types().size() == 1 && entry.param_types()[0] == DataType::TYPEARGS) {
          // If the caller used the kRefAndRaw option, we'll have to convert the params vector from
          // vector<object> to vector<object*>.
          param_pointer = "local_params";
          foper_h << "    const auto count = params.size();" << endl
                  << "    std::vector<PType*> local_params(count);" << endl
                  << "    for (size_t i = 0; i < count; i++) {" << endl
                  << "      local_params[i] = &params[i];" << endl
                  << "    }" << endl;
          foper_h << "    return " << entry.cpp_name() << "(";
        } else {
          param_pointer = "params";
          foper_h << "    return " << entry.cpp_name() << "(";
          pass_result = false;
        }
    }

    string param_end;
    int pindex = 0;
    for (DataType param_type : entry.param_types()) {
      foper_h << param_end;
      param_end = ", ";

      if (param_type == DataType::TYPEARGS) {
        // Break from the loop as we don't allow other argument to be use to TYPE_ARGS.
        foper_h << param_pointer;
        break;
      }

      // Deref the parameters and pass them.
      foper_h << param_pointer << "[" << pindex << "]";
      pindex++;

      // SPECIAL CASE: For CAST operator, at compile time, we need two input parameter for type
      // resolution. However, at runtime, the associated function would take one parameter and
      // convert the value to proper result, so break the loop here.
      if (strcmp(entry.ql_name(), "cast") == 0) {
        break;
      }
    }
    if (!QLType::IsUnknown(entry.return_type())) {
      foper_h << param_end << (pass_result ? "result" : "BFFactory()");
    }
    foper_h << ");" << endl;

    // End of function Exec() and operator class
    foper_h << "  }" << endl;
  }

  void GenerateOpspecTable(string build_dir) {
    // File headers, includes, namespaces, and other declarations.
    ofstream ftable;
    ftable.open(build_dir + "/gen_opspec_table.cc");

    ftable << kFileStart
           << "#include \"yb/bfpg/base_operator.h\"" << endl
           << "#include \"yb/bfpg/directory.h\"" << endl
           << "#include \"yb/bfpg/gen_operator.h\"" << endl
           << "#include \"yb/bfpg/gen_opcodes.h\"" << endl
           << endl
           << "#include <iostream>" << endl
           << "#include <vector>" << endl
           << "#include <functional>" << endl
           << endl
           << "using std::function;" << endl
           << "using std::make_shared;" << endl
           << "using std::vector;" << endl
           << "using std::shared_ptr;" << endl
           << kFileNamespace;

    // Generating table of operators.
    ftable << "const vector<BFOperator::SharedPtr> kBFOperators = {" << endl;
    int op_index = 0;
    for (BFDecl entry : kBFDirectory) {
      const BFClassInfo& bfclass = operator_ids_[op_index];
      ftable << "  make_shared<" << bfclass.class_name << ">("
             << "BFOpcode::" << bfclass.opname << ", "
             << "BFOpcode::" << bfclass.overloaded_opname << ", "
             << "&kBFDirectory[" << op_index << "])," << endl;
      op_index++;
    }
    ftable << "};" << endl
           << endl;

    ftable << kFileEnd;
    ftable.close();
  }

  void GenerateExecTable(string build_dir) {
    // File headers, includes, namespaces, and other declarations.
    ofstream ftable;
    ftable.open(build_dir + "/gen_bfunc_table.cc");

    ftable << kFileStart
           << "#include <iostream>" << endl
           << "#include <vector>" << endl
           << "#include <functional>" << endl
           << endl
           << "#include \"yb/bfpg/bfpg.h\"" << endl
           << "#include \"yb/bfpg/bfunc.h\"" << endl
           << "#include \"yb/bfpg/base_operator.h\"" << endl
           << "#include \"yb/bfpg/gen_operator.h\"" << endl
           << kFileNamespace;

    // Generating table of functions whose outputs are raw pointers.
    ftable << "const BFFunctions BFExecApi::kBFExecFuncsRefAndRaw = {" << endl;
    for (size_t op_index = 0; op_index < operator_ids_.size(); op_index++) {
      const BFClassInfo& bfclass = operator_ids_[op_index];
      ftable << "  " << bfclass.class_name << "::" << "ExecRefAndRaw," << endl;
    }
    ftable << "};" << endl
           << endl;

    ftable << kFileEnd;
    ftable.close();
  }

 private:
  map<string, string> yql2opcode_;
  vector<BFClassInfo> operator_ids_;
};

} // namespace bfpg
} // namespace yb

using yb::bfpg::BFCodegen;

int main(int argc,  char** argv) {
  if (argc < 2) {
    LOG(FATAL) << "Missing directory";
  }

  BFCodegen coder;
  string outdir = argv[1];

  // Form table of opcodes.
  coder.GenerateOpcodes(outdir);
  coder.GenerateOpcodeTable(outdir);

  // Form table of operator specification. This is used to typecheck during compilation.
  coder.GenerateOperators(outdir);
  coder.GenerateOpspecTable(outdir);

  // Form table of exec function pointer. This template is used for builtin execution.
  coder.GenerateExecTable(outdir);

  return 0;
}
