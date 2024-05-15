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
//--------------------------------------------------------------------------------------------------

#pragma once

#include "yb/common/common_fwd.h"
#include "yb/common/ql_datatype.h"
#include "yb/common/value.messages.h"

#include "yb/yql/pggate/util/pg_doc_data.h"
#include "yb/yql/pggate/util/pg_tuple.h"
#include "yb/yql/pggate/pg_table.h"
#include "yb/bfpg/tserver_opcodes.h"

namespace yb {
namespace pggate {

// decode collation encoded string
Slice DecodeCollationEncodedString(Slice input);

class PgColumn;
class PgDml;

class PgExpr {
 public:
  enum class Opcode {
    PG_EXPR_CONSTANT,
    PG_EXPR_COLREF,
    PG_EXPR_VARIABLE,

    // The logical expression for defining the conditions when we support WHERE clause.
    PG_EXPR_NOT,
    PG_EXPR_EQ,
    PG_EXPR_NE,
    PG_EXPR_GE,
    PG_EXPR_GT,
    PG_EXPR_LE,
    PG_EXPR_LT,

    // Aggregate functions.
    PG_EXPR_AVG,
    PG_EXPR_SUM,
    PG_EXPR_COUNT,
    PG_EXPR_MAX,
    PG_EXPR_MIN,

    // Serialized YSQL/PG Expr node.
    PG_EXPR_EVAL_EXPR_CALL,

    PG_EXPR_GENERATE_ROWID,

    PG_EXPR_TUPLE_EXPR,
  };

  // Prepare expression when constructing a statement.
  virtual Status PrepareForRead(PgDml *pg_stmt, LWPgsqlExpressionPB *expr_pb);

  // Gets a vector of PgColumns whose references are nested in this
  // pgexpr.
  virtual Result<std::vector<std::reference_wrapper<PgColumn>>>
      GetColumns(PgTable *pg_table) const;

  // Unpacks all first level nested elements in this pgexpr to a vector of
  // pgexpr.
  virtual std::vector<std::reference_wrapper<const PgExpr>> Unpack() const;

  // Convert this expression structure to PB format.
  Status EvalTo(LWPgsqlExpressionPB *expr_pb);
  Status EvalTo(LWQLValuePB *value);

  virtual Result<LWQLValuePB*> Eval();

  virtual std::string ToString() const;

  // Access methods.
  Opcode opcode() const {
    return opcode_;
  }

  bool is_constant() const {
    return opcode_ == Opcode::PG_EXPR_CONSTANT;
  }

  bool is_colref() const {
    return opcode_ == Opcode::PG_EXPR_COLREF;
  }

  static bool is_aggregate(Opcode opcode) {
    // Only return true for pushdown supported aggregates.
    return opcode == Opcode::PG_EXPR_SUM ||
           opcode == Opcode::PG_EXPR_COUNT ||
           opcode == Opcode::PG_EXPR_MAX ||
           opcode == Opcode::PG_EXPR_MIN;
  }

  bool is_aggregate() const {
    // Only return true for pushdown supported aggregates.
    return is_aggregate(opcode_);
  }

  bool is_tuple_expr() const {
    return opcode_ == Opcode::PG_EXPR_TUPLE_EXPR;
  }

  virtual bool is_ybbasetid() const {
    return false;
  }

  // is expression a system column reference (i.e. oid, ybctid)
  virtual bool is_system() const {
    return false;
  }

  // Read the result from input buffer (yb_cursor) that was computed by and sent from DocDB.
  // Write the result to output buffer (pg_cursor) in Postgres format.
  Status ResultToPg(Slice *yb_cursor, Slice *pg_cursor);

  // Get expression type.
  InternalType internal_type() const;

  // Get Postgres data type information: type Oid, type mod and collation
  int get_pg_typid() const;
  int get_pg_typmod() const;
  int get_pg_collid() const;

  // Find opcode.
  static Status CheckOperatorName(const char *name);
  static Opcode NameToOpcode(const char *name);
  static bfpg::TSOpcode PGOpcodeToTSOpcode(const PgExpr::Opcode opcode);
  static bfpg::TSOpcode OperandTypeToSumTSOpcode(InternalType type);

 protected:
  PgExpr(
      Opcode opcode, const YBCPgTypeEntity *type_entity, bool collate_is_valid_non_c,
      const PgTypeAttrs *type_attrs = nullptr);
  virtual ~PgExpr() = default;

  uint64_t YbToDatum(const void* data, int64_t len) {
    return type_entity_->yb_to_datum(data, len, &type_attrs_);
  }

  // Data members.
  Opcode opcode_;
  const PgTypeEntity *type_entity_;
  bool collate_is_valid_non_c_;
  const PgTypeAttrs type_attrs_;
};

class PgConstant : public PgExpr {
 public:
  PgConstant(ThreadSafeArena* arena,
             const YBCPgTypeEntity *type_entity,
             bool collate_is_valid_non_c,
             const char* collation_sortkey,
             uint64_t datum,
             bool is_null,
             PgExpr::Opcode opcode = PgExpr::Opcode::PG_EXPR_CONSTANT);
  PgConstant(ThreadSafeArena* arena,
             const YBCPgTypeEntity *type_entity,
             bool collate_is_valid_non_c,
             PgDatumKind datum_kind,
             PgExpr::Opcode opcode = PgExpr::Opcode::PG_EXPR_CONSTANT);

  // Update numeric.
  void UpdateConstant(int8_t value, bool is_null);
  void UpdateConstant(int16_t value, bool is_null);
  void UpdateConstant(int32_t value, bool is_null);
  void UpdateConstant(int64_t value, bool is_null);
  void UpdateConstant(float value, bool is_null);
  void UpdateConstant(double value, bool is_null);

  // Update text.
  void UpdateConstant(const char *value, bool is_null);
  void UpdateConstant(const void *value, size_t bytes, bool is_null);

  // Expression to PB.
  Result<LWQLValuePB*> Eval() override;

  std::string ToString() const override;

  // Read binary value.
  Slice binary_value() {
    return ql_value_.binary_value();
  }

 private:
  LWQLValuePB ql_value_;
};

class PgFetchedTarget {
 public:
  virtual ~PgFetchedTarget() = default;

  virtual void SetNull(PgTuple* out) = 0;
  virtual void SetValue(Slice* data, PgTuple* out) = 0;
};

class PgColumnRef : public PgExpr, public PgFetchedTarget {
 public:
  // Setup ColumnRef expression when constructing statement.
  Status PrepareForRead(PgDml *pg_stmt, LWPgsqlExpressionPB *expr_pb) override;

  Result<std::vector<std::reference_wrapper<PgColumn>>>
  GetColumns(PgTable *pg_table) const override;

  bool is_ybbasetid() const override;

  bool is_system() const override;

  int attr_num() const {
    return attr_num_;
  }

  static PgColumnRef* Create(
      ThreadSafeArena* arena,
      int attr_num,
      const PgTypeEntity* type_entity,
      bool collate_is_valid_non_c,
      const PgTypeAttrs *type_attrs);

 protected:
  template <class... Args>
  PgColumnRef(int attr_num, Args&&... args)
      : PgExpr(PgExpr::Opcode::PG_EXPR_COLREF, std::forward<Args>(args)...), attr_num_(attr_num) {}

 private:
  const int attr_num_;
};

class PgOperator : public PgExpr {
 public:
  PgOperator(ThreadSafeArena* arena,
             Opcode opcode,
             const YBCPgTypeEntity *type_entity,
             bool collate_is_valid_non_c);

  // Append arguments.
  void AppendArg(PgExpr *arg);

  // Setup operator expression when constructing statement.
  virtual Status PrepareForRead(PgDml *pg_stmt, LWPgsqlExpressionPB *expr_pb);

  static PgOperator* Create(
      ThreadSafeArena* arena,
      const char *name,
      const YBCPgTypeEntity *type_entity,
      bool collate_is_valid_non_c);

 private:
  ArenaList<PgExpr> args_;
};

class PgAggregateOperator : public PgOperator, public PgFetchedTarget {
 public:
  template <class... Args>
  explicit PgAggregateOperator(Args&&... args) : PgOperator(std::forward<Args>(args)...) {}

  void SetNull(PgTuple* tuple) override {
    tuple->WriteNull(index());
  }

  int index() const {
    return index_;
  }

  void set_index(int value) {
    index_ = value;
  }

 protected:
  void DoSetDatum(PgTuple* tuple, uint64_t datum);

  int index_ = -1;
};

class PgTupleExpr : public PgExpr {
 public:
  PgTupleExpr(ThreadSafeArena* arena,
              const YBCPgTypeEntity* type_entity,
              const PgTypeAttrs *type_attrs,
              int num_elems,
              PgExpr *const *elems);

  Status PrepareForRead(PgDml *pg_stmt, LWPgsqlExpressionPB *expr_pb) override;

  Result<std::vector<std::reference_wrapper<PgColumn>>>
  GetColumns(PgTable *pg_table) const override;

  Result<LWQLValuePB*> Eval() override;

  const ArenaList<PgExpr> &GetElems() const { return elems_; }

  std::vector<std::reference_wrapper<const PgExpr>> Unpack() const override;

 private:
  ArenaList<PgExpr> elems_;
  LWQLValuePB ql_tuple_expr_value_;
};

}  // namespace pggate
}  // namespace yb
