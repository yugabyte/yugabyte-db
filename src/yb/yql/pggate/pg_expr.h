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

#include "yb/client/client.h"
#include "yb/yql/pggate/util/pg_doc_data.h"
#include "yb/yql/pggate/util/pg_tuple.h"

namespace yb {
namespace pggate {

class PgDml;
class PgExpr {
 public:
  enum class Opcode {
    PG_EXPR_CONSTANT,
    PG_EXPR_COLREF,
    PG_EXPR_VARIABLE,

    // The following is not yet implemented. The logical expression will be used to define the
    // conditions when we support WHERE clause.
    PG_EXPR_EQ,
    PG_EXPR_NE,
    PG_EXPR_GE,
    PG_EXPR_GT,
    PG_EXPR_LE,
    PG_EXPR_LT,

    PG_EXPR_AVG,
    PG_EXPR_SUM,
    PG_EXPR_COUNT,
    PG_EXPR_MAX,
    PG_EXPR_MIN,
  };

  // Public types.
  typedef std::shared_ptr<PgExpr> SharedPtr;
  typedef std::shared_ptr<const PgExpr> SharedPtrConst;

  typedef std::unique_ptr<PgExpr> UniPtr;
  typedef std::unique_ptr<const PgExpr> UniPtrConst;

  // Constructor.
  explicit PgExpr(Opcode op, InternalType internal_type = InternalType::VALUE_NOT_SET);
  virtual ~PgExpr();

  // Prepare expression when constructing a statement.
  virtual CHECKED_STATUS Prepare(PgDml *pg_stmt, PgsqlExpressionPB *expr_pb);

  // Convert this expression structure to PB format.
  virtual CHECKED_STATUS Eval(PgDml *pg_stmt, PgsqlExpressionPB *expr_pb);

  // Access methods.
  Opcode op() const {
    return op_;
  }

  // Read the result from input buffer (yb_cursor) that was computed by and sent from DocDB.
  // Write the result to output buffer (pg_cursor) in Postgres format.
  CHECKED_STATUS ResultToPg(Slice *yb_cursor, Slice *pg_cursor);

  // Translate data to DocDB to Postgres format.
  std::function<void(Slice *, const PgWireDataHeader&, PgTuple *, int)> TranslateData;

  template<typename data_type>
  static void TranslateNumber(Slice *yb_cursor, const PgWireDataHeader& header,
                              PgTuple *pg_tuple, int index) {
    if (header.is_null()) {
      return pg_tuple->WriteNull(index, header);
    }
    data_type result = 0;
    size_t read_size = PgDocData::ReadNumber(yb_cursor, &result);
    yb_cursor->remove_prefix(read_size);
    pg_tuple->Write(index, header, result);
  }

  static void TranslateText(Slice *yb_cursor, const PgWireDataHeader& header,
                            PgTuple *pg_tuple, int index);
  static void TranslateComplex(Slice *wire_data, const PgWireDataHeader& header,
                               PgTuple *pg_tuple, int index);

  InternalType internal_type() const {
    return internal_type_;
  }

 protected:
  Opcode op_;
  InternalType internal_type_;
};

class PgConstant : public PgExpr {
 public:
  // Public types.
  typedef std::shared_ptr<PgConstant> SharedPtr;
  typedef std::shared_ptr<const PgConstant> SharedPtrConst;

  typedef std::unique_ptr<PgConstant> UniPtr;
  typedef std::unique_ptr<const PgConstant> UniPtrConst;

  // Numeric constant.
  explicit PgConstant(int16_t value, bool is_null);
  explicit PgConstant(int32_t value, bool is_null);
  explicit PgConstant(int64_t value, bool is_null);
  explicit PgConstant(float value, bool is_null);
  explicit PgConstant(double value, bool is_null);

  // Character string constant.
  PgConstant(const char *value, bool is_null);
  PgConstant(const char *value, size_t bytes, bool is_null);

  // Destructor.
  virtual ~PgConstant();

  // Update numeric.
  void UpdateConstant(int16_t value, bool is_null);
  void UpdateConstant(int32_t value, bool is_null);
  void UpdateConstant(int64_t value, bool is_null);
  void UpdateConstant(float value, bool is_null);
  void UpdateConstant(double value, bool is_null);

  // Update text.
  void UpdateConstant(const char *value, bool is_null);
  void UpdateConstant(const char *value, size_t bytes, bool is_null);

  // Expression to PB.
  virtual CHECKED_STATUS Eval(PgDml *pg_stmt, PgsqlExpressionPB *expr_pb);

 private:
  QLValuePB ql_value_;
};

class PgColumnRef : public PgExpr {
 public:
  // Public types.
  typedef std::shared_ptr<PgColumnRef> SharedPtr;
  typedef std::shared_ptr<const PgColumnRef> SharedPtrConst;

  typedef std::unique_ptr<PgColumnRef> UniPtr;
  typedef std::unique_ptr<const PgColumnRef> UniPtrConst;

  explicit PgColumnRef(int attr_num);
  virtual ~PgColumnRef();

  // Setup ColumnRef expression when constructing statement.
  virtual CHECKED_STATUS Prepare(PgDml *pg_stmt, PgsqlExpressionPB *expr_pb);

  int attr_num() const {
    return attr_num_;
  }

 private:
  int attr_num_;
};

}  // namespace pggate
}  // namespace yb

#endif // YB_YQL_PGGATE_PG_EXPR_H_
