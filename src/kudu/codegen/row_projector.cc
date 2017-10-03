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

#include "kudu/codegen/row_projector.h"

#include <algorithm>
#include <string>
#include <ostream>
#include <utility>
#include <vector>

#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/IR/Argument.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>

#include "kudu/codegen/jit_wrapper.h"
#include "kudu/codegen/module_builder.h"
#include "kudu/common/row.h"
#include "kudu/common/schema.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/gutil/strings/strcat.h"
#include "kudu/util/faststring.h"
#include "kudu/util/status.h"

namespace llvm {
class LLVMContext;
} // namespace llvm

using llvm::Argument;
using llvm::BasicBlock;
using llvm::ConstantInt;
using llvm::ExecutionEngine;
using llvm::Function;
using llvm::FunctionType;
using llvm::GenericValue;
using llvm::LLVMContext;
using llvm::Module;
using llvm::PointerType;
using llvm::Type;
using llvm::Value;
using std::ostream;
using std::string;
using std::unique_ptr;
using std::vector;

DECLARE_bool(codegen_dump_functions);

namespace kudu {
namespace codegen {

namespace {

// Generates a schema-to-schema projection function of the form:
// bool(int8_t* src, RowBlockRow* row, Arena* arena)
// Requires src is a contiguous row of the base schema.
// Returns a boolean indicating success. Failure can only occur if a string
// relocation fails.
//
// Uses CHECKs to make sure projection is well-formed. Use
// kudu::RowProjector::Init() to return an error status instead.
template<bool READ>
llvm::Function* MakeProjection(const string& name,
                               ModuleBuilder* mbuilder,
                               const kudu::RowProjector& proj) {
  // Get the IRBuilder
  ModuleBuilder::LLVMBuilder* builder = mbuilder->builder();
  LLVMContext& context = builder->getContext();

  // Extract schema information from projector
  const Schema& base_schema = *proj.base_schema();
  const Schema& projection = *proj.projection();

  // Create the function after providing a declaration
  vector<Type*> argtypes = { Type::getInt8PtrTy(context),
                             PointerType::getUnqual(mbuilder->GetType("class.kudu::RowBlockRow")),
                             PointerType::getUnqual(mbuilder->GetType("class.kudu::Arena")) };
  FunctionType* fty =
    FunctionType::get(Type::getInt1Ty(context), argtypes, false);
  Function* f = mbuilder->Create(fty, name);

  // Get the function's Arguments
  Function::arg_iterator it = f->arg_begin();
  Argument* src = &*it++;
  Argument* rbrow = &*it++;
  Argument* arena = &*it++;
  DCHECK(it == f->arg_end());

  // Give names to the arguments for debugging IR.
  src->setName("src");
  rbrow->setName("rbrow");
  arena->setName("arena");

  // Mark our arguments as not aliasing. This eliminates a redundant
  // load of rbrow->row_block_ and rbrow->row_index_ for each column.
  // Note that these arguments are 1-based indexes.
  f->setDoesNotAlias(1);
  f->setDoesNotAlias(2);
  f->setDoesNotAlias(3);

  // Project row function in IR (note: values in angle brackets are
  // constants whose values are determined right now, at JIT time).
  //
  // define i1 @name(i8* noalias %src, RowBlockRow* noalias %rbrow, Arena* noalias %arena)
  // entry:
  //   %src_bitmap = getelementptr i8* %src, i64 <offset to bitmap>
  //   <for each base column to projection column mapping>
  //     %src_cell = getelementptr i8* %src, i64 <base offset>
  //     %result = call i1 @CopyCellToRowBlock(
  //       i64 <type size>, i8* %src_cell, RowBlockRow* %rbrow,
  //       i64 <column index>, i1 <is binary>, Arena* %arena)**
  //   %success = and %success, %result***
  //   <end implicit for each>
  //   <for each projection column that needs defaults>
  //     <if default column is nullable>
  //       call void @CopyCellToRowBlockNullDefault(
  //         RowBlockRow* %rbrow, i64 <column index>, i1 <is null>)
  //     <end implicit if>
  //     <if default value was not null>
  //       %src_cell = inttoptr i64 <default value location> to i8*
  //       %result = call i1 @CopyCellToRowBlock(
  //         i64 <type size>, i8* %src_cell, RowBlockRow* %rbrow,
  //         i64 <column index>, i1 <is_binary>, Arena* %arena)
  //       %success = and %success, %result***
  //     <end implicit if>
  //   <end implicit for each>
  //   ret i1 %success
  //
  // **If the column is nullable, then the call is replaced with
  // call i1 @CopyCellToRowBlockNullable(
  //   i64 <type size>, i8* %src_cell, RowBlockRow* %rbrow, i64 <column index>,
  //   i1 <is_binary>, Arena* %arena, i8* src_bitmap, i64 <bitmap_idx>)
  // ***If the column is nullable and the default value is NULL, then the
  // call is replaced with
  // call void @CopyCellToRowBlockSetNull(
  //   RowBlockRow* %rbrow, i64 <column index>)
  // ****Technically, llvm ir does not support mutable registers. Thus,
  // this is implemented by having "success" be the most recent result
  // register of the last "and" instruction. The different "success" values
  // can be differentiated by using a success_update_number.

  // Retrieve appropriate precompiled rowblock cell functions
  Function* copy_cell_not_null =
    mbuilder->GetFunction("_PrecompiledCopyCellToRowBlock");
  Function* copy_cell_nullable =
    mbuilder->GetFunction("_PrecompiledCopyCellToRowBlockNullable");
  Function* row_block_set_null =
    mbuilder->GetFunction("_PrecompiledCopyCellToRowBlockSetNull");

  // The bitmap for a contiguous row goes after the row data
  // See common/row.h ContiguousRowHelper class
  builder->SetInsertPoint(BasicBlock::Create(context, "entry", f));
  Value* src_bitmap = builder->CreateConstGEP1_64(src, base_schema.byte_size());
  src_bitmap->setName("src_bitmap");
  Value* success = builder->getInt1(true);
  int success_update_number = 0;

  // Copy base data
  for (const kudu::RowProjector::ProjectionIdxMapping& pmap : proj.base_cols_mapping()) {
    // Retrieve information regarding this column-to-column transformation
    size_t proj_idx = pmap.first;
    size_t base_idx = pmap.second;
    size_t src_offset = base_schema.column_offset(base_idx);
    const ColumnSchema& col = base_schema.column(base_idx);

    // Create the common values between the nullable and nonnullable calls
    Value* size = builder->getInt64(col.type_info()->size());
    Value* src_cell = builder->CreateConstGEP1_64(src, src_offset);
    src_cell->setName(StrCat("src_cell_base_", base_idx));
    Value* col_idx = builder->getInt64(proj_idx);
    ConstantInt* is_binary = builder->getInt1(col.type_info()->physical_type() == BINARY);
    vector<Value*> args = { size, src_cell, rbrow, col_idx, is_binary, arena };

    // Add additional arguments if nullable
    Function* to_call = copy_cell_not_null;
    if (col.is_nullable()) {
      args.push_back(src_bitmap);
      args.push_back(builder->getInt64(base_idx));
      to_call = copy_cell_nullable;
    }

    // Make the call and check the return value
    Value* result = builder->CreateCall(to_call, args);
    result->setName(StrCat("result_b", base_idx, "_p", proj_idx));
    success = builder->CreateAnd(success, result);
    success->setName(StrCat("success", success_update_number++));
  }

  // TODO: Copy adapted base data
  DCHECK(proj.adapter_cols_mapping().size() == 0)
    << "Value Adapter not supported yet";

  // Fill defaults
  for (size_t dfl_idx : proj.projection_defaults()) {
    // Retrieve mapping information
    const ColumnSchema& col = projection.column(dfl_idx);
    const void* dfl = READ ? col.read_default_value() :
      col.write_default_value();

    // Generate arguments
    Value* size = builder->getInt64(col.type_info()->size());
    Value* src_cell = mbuilder->GetPointerValue(const_cast<void*>(dfl));
    Value* col_idx = builder->getInt64(dfl_idx);
    ConstantInt* is_binary = builder->getInt1(col.type_info()->physical_type() == BINARY);

    // Handle default columns that are nullable
    if (col.is_nullable()) {
      Value* is_null = builder->getInt1(dfl == nullptr);
      vector<Value*> args = { rbrow, col_idx, is_null };
      builder->CreateCall(row_block_set_null, args);
      // If dfl was NULL, we're done
      if (dfl == nullptr) continue;
    }

    // Make the copy cell call and check the return value
    vector<Value*> args = { size, src_cell, rbrow, col_idx, is_binary, arena };
    Value* result = builder->CreateCall(copy_cell_not_null, args);
    result->setName(StrCat("result_dfl", dfl_idx));
    success = builder->CreateAnd(success, result);
    success->setName(StrCat("success", success_update_number++));
  }

  // Return
  builder->CreateRet(success);

  if (FLAGS_codegen_dump_functions) {
    LOG(INFO) << "Dumping " << (READ? "read" : "write") << " projection:";
    f->dump();
  }

  return f;
}

} // anonymous namespace

RowProjectorFunctions::RowProjectorFunctions(const Schema& base_schema,
                                             const Schema& projection,
                                             ProjectionFunction read_f,
                                             ProjectionFunction write_f,
                                             unique_ptr<JITCodeOwner> owner)
  : JITWrapper(std::move(owner)),
    base_schema_(base_schema),
    projection_(projection),
    read_f_(read_f),
    write_f_(write_f) {
  CHECK(read_f != nullptr)
    << "Promise to compile read function not fulfilled by ModuleBuilder";
  CHECK(write_f != nullptr)
    << "Promise to compile write function not fulfilled by ModuleBuilder";
}

Status RowProjectorFunctions::Create(const Schema& base_schema,
                                     const Schema& projection,
                                     scoped_refptr<RowProjectorFunctions>* out,
                                     llvm::TargetMachine** tm) {
  ModuleBuilder builder;
  RETURN_NOT_OK(builder.Init());

  // Use a no-codegen row projector to check validity and to build
  // the codegen functions.
  kudu::RowProjector no_codegen(&base_schema, &projection);
  RETURN_NOT_OK(no_codegen.Init());

  // Build the functions for code gen. No need to mangle for uniqueness;
  // in the rare case we have two projectors in one module, LLVM takes
  // care of uniquifying when making a GlobalValue.
  Function* read = MakeProjection<true>("ProjRead", &builder, no_codegen);
  Function* write = MakeProjection<false>("ProjWrite", &builder, no_codegen);

  // Have the ModuleBuilder accept promises to compile the functions
  ProjectionFunction read_f, write_f;
  builder.AddJITPromise(read, &read_f);
  builder.AddJITPromise(write, &write_f);

  unique_ptr<JITCodeOwner> owner;
  RETURN_NOT_OK(builder.Compile(&owner));

  if (tm) {
    *tm = builder.GetTargetMachine();
  }
  out->reset(new RowProjectorFunctions(base_schema, projection, read_f,
                                       write_f, std::move(owner)));
  return Status::OK();
}

namespace {
// Convenience method which appends to a faststring
template<typename T>
void AddNext(faststring* fs, const T& val) {
  fs->append(&val, sizeof(T));
}
} // anonymous namespace

// Allocates space for and generates a key for a pair of schemas. The key
// is unique according to the criteria defined in the CodeCache class'
// block comment. In order to achieve this, we encode the schemas into
// a contiguous array of bytes as follows, in sequence.
//
// (1 byte) unique type identifier for RowProjectorFunctions
// (8 bytes) number, as unsigned long, of base columns
// (5 bytes each) base column types, in order
//   4 bytes for enum type
//   1 byte for nullability
// (8 bytes) number, as unsigned long, of projection columns
// (5 bytes each) projection column types, in order
//   4 bytes for enum type
//   1 byte for nullablility
// (8 bytes) number, as unsigned long, of base projection mappings
// (16 bytes each) base projection mappings, in order
// (24 bytes each) default projection columns, in order
//   8 bytes for the index
//   8 bytes for the read default
//   8 bytes for the write default
//
// This could be made more efficient by removing unnecessary information
// such as the top bits for many numbers, and using a thread-local buffer
// (the code cache copies its own key anyway).
// Respects IsCompatbile below.
//
// Writes to 'out' upon success.
Status RowProjectorFunctions::EncodeKey(const Schema& base, const Schema& proj,
                                        faststring* out) {
  kudu::RowProjector projector(&base, &proj);
  RETURN_NOT_OK(projector.Init());

  AddNext(out, JITWrapper::ROW_PROJECTOR);
  AddNext(out, base.num_columns());
  for (const ColumnSchema& col : base.columns()) {
    AddNext(out, col.type_info()->physical_type());
    AddNext(out, col.is_nullable());
  }
  AddNext(out, proj.num_columns());
  for (const ColumnSchema& col : proj.columns()) {
    AddNext(out, col.type_info()->physical_type());
    AddNext(out, col.is_nullable());
  }
  AddNext(out, projector.base_cols_mapping().size());
  for (const kudu::RowProjector::ProjectionIdxMapping& map : projector.base_cols_mapping()) {
    AddNext(out, map);
  }
  for (size_t dfl_idx : projector.projection_defaults()) {
    const ColumnSchema& col = proj.column(dfl_idx);
    AddNext(out, dfl_idx);
    AddNext(out, col.read_default_value());
    AddNext(out, col.write_default_value());
  }

  return Status::OK();
}

RowProjector::RowProjector(const Schema* base_schema, const Schema* projection,
                           const scoped_refptr<RowProjectorFunctions>& functions)
  : projector_(base_schema, projection),
    functions_(functions) {}

namespace {

struct DefaultEquals {
  template<class T>
  bool operator()(const T& t1, const T& t2) { return t1 == t2; }
};

struct ColumnSchemaEqualsType {
  bool operator()(const ColumnSchema& s1, const ColumnSchema& s2) {
    return s1.EqualsType(s2);
  }
};

template<class T, class Equals>
bool ContainerEquals(const T& t1, const T& t2, const Equals& equals) {
  if (t1.size() != t2.size()) return false;
  if (!std::equal(t1.begin(), t1.end(), t2.begin(), equals)) return false;
  return true;
}

template<class T>
bool ContainerEquals(const T& t1, const T& t2) {
  return ContainerEquals(t1, t2, DefaultEquals());
}

// This method defines what makes (base, projection) schema pairs compatible.
// In other words, this method can be thought of as the equivalence relation
// on the set of all well-formed (base, projection) schema pairs that
// partitions the set into equivalence classes which will have the exact
// same projection function code.
//
// This equivalence relation can be decomposed as:
//
//   ProjectionsCompatible((base1, proj1), (base2, proj2)) :=
//     WELLFORMED(base1, proj1) &&
//     WELLFORMED(base2, proj2) &&
//     PROJEQUALS(base1, base2) &&
//     PROJEQUALS(proj1, proj2) &&
//     MAP(base1, proj1) == MAP(base2, proj2)
//
// where WELLFORMED checks that a projection is well-formed (i.e., a
// kudu::RowProjector can be initialized with the schema pair), PROJEQUAL
// is a relaxed version of the Schema::Equals() operator that is
// independent of column names and column IDs, and MAP addresses
// the actual dependency on column identification - which is the effect
// that those attributes have on the RowProjector's mapping (i.e., different
// names and IDs are ok, so long as the mapping is the same). Note that
// key columns are not given any special meaning in projection. Types
// and nullability of columns must be exactly equal between the two
// schema pairs.
//
// Status::OK corresponds to true in the equivalence relation and other
// statuses correspond to false, explaining why the projections are
// incompatible.
Status ProjectionsCompatible(const Schema& base1, const Schema& proj1,
                             const Schema& base2, const Schema& proj2) {
  kudu::RowProjector rp1(&base1, &proj1), rp2(&base2, &proj2);
  RETURN_NOT_OK_PREPEND(rp1.Init(), "(base1, proj1) projection "
                        "schema pair not well formed: ");
  RETURN_NOT_OK_PREPEND(rp2.Init(), "(base2, proj2) projection "
                        "schema pair not well formed: ");

  if (!ContainerEquals(base1.columns(), base2.columns(),
                       ColumnSchemaEqualsType())) {
    return Status::IllegalState("base schema types unequal");
  }
  if (!ContainerEquals(proj1.columns(), proj2.columns(),
                       ColumnSchemaEqualsType())) {
    return Status::IllegalState("projection schema types unequal");
  }

  if (!ContainerEquals(rp1.base_cols_mapping(), rp2.base_cols_mapping())) {
    return Status::IllegalState("base column mappings do not match");
  }
  if (!ContainerEquals(rp1.adapter_cols_mapping(), rp2.adapter_cols_mapping())) {
    return Status::IllegalState("adapter column mappings do not match");
  }
  if (!ContainerEquals(rp1.projection_defaults(), rp2.projection_defaults())) {
    return Status::IllegalState("projection default indices do not match");
  }

  return Status::OK();
}

} // anonymous namespace

Status RowProjector::Init() {
  RETURN_NOT_OK(projector_.Init());
#ifndef NDEBUG
  RETURN_NOT_OK_PREPEND(ProjectionsCompatible(
                          *projector_.base_schema(), *projector_.projection(),
                          functions_->base_schema(), functions_->projection()),
                        "Codegenned row projector's schemas incompatible "
                        "with its functions' schemas:"
                        "\n  projector base = " +
                        projector_.base_schema()->ToString() +
                        "\n  projector proj = " +
                        projector_.projection()->ToString() +
                        "\n  functions base = " +
                        functions_->base_schema().ToString() +
                        "\n  functions proj = " +
                        functions_->projection().ToString());
#endif
  return Status::OK();
}

ostream& operator<<(ostream& o, const RowProjector& rp) {
  o << "Row Projector s1->s2 with:\n"
    << "\ts1 = " << rp.base_schema()->ToString() << "\n"
    << "\ts2 = " << rp.projection()->ToString();
  return o;
}

} // namespace codegen
} // namespace kudu
