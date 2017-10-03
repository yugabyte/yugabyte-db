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

#ifndef KUDU_CODEGEN_ROW_PROJECTOR_H
#define KUDU_CODEGEN_ROW_PROJECTOR_H

#include <iosfwd>
#include <memory>
#include <vector>

#include "kudu/codegen/jit_wrapper.h"
#include "kudu/common/row.h"
#include "kudu/common/rowblock.h"
#include "kudu/common/schema.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/util/slice.h"
#include "kudu/util/status.h"

namespace llvm {
class TargetMachine;
} // namespace llvm

namespace kudu {

class Arena;

namespace codegen {

// The JITWrapper for codegen::RowProjector functions. Contains
// the compiled functions themselves as well as the schemas used
// to generate them.
class RowProjectorFunctions : public JITWrapper {
 public:
  // Compiles the row projector functions for the given base
  // and projection.
  // Writes the llvm::TargetMachine* used to 'tm' (if not NULL)
  // and the functions to 'out' upon success.
  static Status Create(const Schema& base_schema, const Schema& projection,
                       scoped_refptr<RowProjectorFunctions>* out,
                       llvm::TargetMachine** tm = NULL);

  const Schema& base_schema() { return base_schema_; }
  const Schema& projection() { return projection_; }

  typedef bool(*ProjectionFunction)(const uint8_t*, RowBlockRow*, Arena*);
  ProjectionFunction read() const { return read_f_; }
  ProjectionFunction write() const { return write_f_; }

  virtual Status EncodeOwnKey(faststring* out) OVERRIDE {
    return EncodeKey(base_schema_, projection_, out);
  }

  static Status EncodeKey(const Schema& base, const Schema& proj,
                          faststring* out);

 private:
  RowProjectorFunctions(const Schema& base_schema, const Schema& projection,
                        ProjectionFunction read_f, ProjectionFunction write_f,
                        std::unique_ptr<JITCodeOwner> owner);

  const Schema base_schema_, projection_;
  const ProjectionFunction read_f_, write_f_;
};

// This projector behaves the almost the same way as a tablet/RowProjector except that
// it only supports certain row types, and expects a regular Arena. Furthermore,
// the Reset() public method is unsupported.
//
// See documentation for RowProjector. Any differences in the API will be explained
// in this class.
class RowProjector {
 public:
  typedef kudu::RowProjector::ProjectionIdxMapping ProjectionIdxMapping;

  // Requires that both schemas remain valid for the lifetime of this
  // object. Also requires that the schemas are compatible with
  // the schemas used to create 'functions'.
  RowProjector(const Schema* base_schema, const Schema* projection,
               const scoped_refptr<RowProjectorFunctions>& code);

  Status Init();

  // Ignores relocations if dst_arena == NULL
  template<class ContiguousRowType>
  Status ProjectRowForRead(const ContiguousRowType& src_row,
                           RowBlockRow* dst_row,
                           Arena* dst_arena) const {
    DCHECK_SCHEMA_EQ(*base_schema(), *src_row.schema());
    DCHECK_SCHEMA_EQ(*projection(), *dst_row->schema());
    RowProjectorFunctions::ProjectionFunction f = functions_->read();
    if (PREDICT_TRUE(f(src_row.row_data(), dst_row, dst_arena))) {
      return Status::OK();
    }
    return Status::IOError("out of memory copying slice during projection. "
                           "Base schema row: ", base_schema()->DebugRow(src_row));
  }

  // Warning: the projection schema should have write-defaults defined
  // if it has default columns. There was no check for default write
  // columns during this class' initialization.
  // Ignores relocations if dst_arena == NULL
  template<class ContiguousRowType>
  Status ProjectRowForWrite(const ContiguousRowType& src_row,
                            RowBlockRow* dst_row,
                            Arena* dst_arena) const {
    DCHECK_SCHEMA_EQ(*base_schema(), *src_row.schema());
    DCHECK_SCHEMA_EQ(*projection(), *dst_row->schema());
    RowProjectorFunctions::ProjectionFunction f = functions_->write();
    if (PREDICT_TRUE(f(src_row.row_data(), dst_row, dst_arena))) {
      return Status::OK();
    }
    return Status::IOError("out of memory copying slice during projection. "
                           "Base schema row: ", base_schema()->DebugRow(src_row));
  }

  const vector<ProjectionIdxMapping>& base_cols_mapping() const {
    return projector_.base_cols_mapping();
  }
  const vector<ProjectionIdxMapping>& adapter_cols_mapping() const {
    return projector_.adapter_cols_mapping();
  }
  const vector<size_t>& projection_defaults() const {
    return projector_.projection_defaults();
  }
  bool is_identity() const { return projector_.is_identity(); }
  const Schema* projection() const { return projector_.projection(); }
  const Schema* base_schema() const { return projector_.base_schema(); }

 private:
  kudu::RowProjector projector_;
  scoped_refptr<RowProjectorFunctions> functions_;

  DISALLOW_COPY_AND_ASSIGN(RowProjector);
};

extern std::ostream& operator<<(std::ostream& o, const RowProjector& rp);

} // namespace codegen
} // namespace kudu

#endif
