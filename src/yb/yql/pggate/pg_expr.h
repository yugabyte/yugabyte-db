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

#ifndef YB_YQL_PGGATE_PG_EXPR_H_
#define YB_YQL_PGGATE_PG_EXPR_H_

#include "yb/common/common_fwd.h"
#include "yb/common/ql_datatype.h"
#include "yb/common/value.messages.h"

#include "yb/yql/pggate/util/pg_doc_data.h"
#include "yb/yql/pggate/util/pg_tuple.h"
#include "yb/bfpg/tserver_opcodes.h"

namespace yb {
namespace pggate {

// decode collation encoded string
void DecodeCollationEncodedString(const char** text, int64_t* text_len);

class PgDml;

using DataTranslator = void(*)(
    Slice* yb_cursor, const PgWireDataHeader& header, int index,
    const YBCPgTypeEntity* type_entity, const PgTypeAttrs *type_attrs, PgTuple *pg_tuple);

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
  };

  // Prepare expression when constructing a statement.
  virtual Status PrepareForRead(PgDml *pg_stmt, LWPgsqlExpressionPB *expr_pb);

  // Convert this expression structure to PB format.
  Status EvalTo(LWPgsqlExpressionPB *expr_pb);
  Status EvalTo(LWQLValuePB *value);

  virtual Result<LWQLValuePB*> Eval();

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
  bool is_aggregate() const {
    // Only return true for pushdown supported aggregates.
    return (opcode_ == Opcode::PG_EXPR_SUM ||
            opcode_ == Opcode::PG_EXPR_COUNT ||
            opcode_ == Opcode::PG_EXPR_MAX ||
            opcode_ == Opcode::PG_EXPR_MIN);
  }
  virtual bool is_ybbasetid() const {
    return false;
  }

  // Read the result from input buffer (yb_cursor) that was computed by and sent from DocDB.
  // Write the result to output buffer (pg_cursor) in Postgres format.
  Status ResultToPg(Slice *yb_cursor, Slice *pg_cursor);

  // Function translate_data_() reads the received data from DocDB and writes it to Postgres buffer
  // using to_datum().
  // - DocDB supports a number of datatypes, and we would need to provide one translate function for
  //   each datatype to read them correctly. The translate_data() function pointer must be setup
  //   correctly during the compilation of a statement.
  // - For each postgres data type, to_datum() function pointer must be setup properly during
  //   the compilation of a statement.
  void TranslateData(Slice *yb_cursor, const PgWireDataHeader& header, int index,
                     PgTuple *pg_tuple) const;

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

  void InitializeTranslateData();

  // Data members.
  Opcode opcode_;
  const PgTypeEntity *type_entity_;
  bool collate_is_valid_non_c_;
  const PgTypeAttrs type_attrs_;
  DataTranslator translate_data_;
};

class PgConstant : public PgExpr {
 public:
  PgConstant(Arena* arena,
             const YBCPgTypeEntity *type_entity,
             bool collate_is_valid_non_c,
             const char* collation_sortkey,
             uint64_t datum,
             bool is_null,
             PgExpr::Opcode opcode = PgExpr::Opcode::PG_EXPR_CONSTANT);
  PgConstant(Arena* arena,
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

  // Read binary value.
  Slice binary_value() {
    return ql_value_.binary_value();
  }

 private:
  LWQLValuePB ql_value_;
  const char *collation_sortkey_;
};

class PgColumnRef : public PgExpr {
 public:
  PgColumnRef(int attr_num,
              const PgTypeEntity *type_entity,
              bool collate_is_valid_non_c,
              const PgTypeAttrs *type_attrs);
  // Setup ColumnRef expression when constructing statement.
  Status PrepareForRead(PgDml *pg_stmt, LWPgsqlExpressionPB *expr_pb) override;

  int attr_num() const {
    return attr_num_;
  }

  bool is_ybbasetid() const override;

 private:
  int attr_num_;
};

class PgOperator : public PgExpr {
 public:
  PgOperator(Arena* arena,
             const char *name,
             const YBCPgTypeEntity *type_entity,
             bool collate_is_valid_non_c);

  // Append arguments.
  void AppendArg(PgExpr *arg);

  // Setup operator expression when constructing statement.
  virtual Status PrepareForRead(PgDml *pg_stmt, LWPgsqlExpressionPB *expr_pb);

 private:
  Slice opname_;
  ArenaList<PgExpr> args_;
};

}  // namespace pggate
}  // namespace yb

#endif // YB_YQL_PGGATE_PG_EXPR_H_
