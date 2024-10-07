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

#include "yb/docdb/docdb_pgapi.h"

#include "yb/common/pg_types.h"
#include "yb/common/schema.h"

#include "yb/dockv/pg_row.h"
#include "yb/dockv/reader_projection.h"

#include "yb/gutil/singleton.h"

#include "yb/qlexpr/ql_expr.h"

#include "yb/util/logging.h"
#include "yb/util/result.h"

#include "yb/yql/pggate/pg_expr.h"
#include "yb/yql/pggate/pg_value.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"
#include "yb/yql/pggate/ybc_pggate.h"

#include "ybgate/ybgate_cpp_util.h"
#include "ybgate/ybgate_status.h"

#include "catalog/pg_type_d.h"

using std::string;

namespace yb::docdb {

using pggate::PgValueToDatum;

#define SET_ELEM_LEN_BYVAL_ALIGN(elemlen, elembyval, elemalign) \
  do { \
    elmlen = elemlen; \
    elmbyval = elembyval; \
    elmalign = elemalign; \
  } while (0);

#define SET_ELEM_LEN_BYVAL_ALIGN_OPT(elemlen, elembyval, elemalign, opt) \
  do { \
    elmlen = elemlen; \
    elmbyval = elembyval; \
    elmalign = elemalign; \
    option = opt; \
  } while (0);

#define SET_RANGE_ELEM_LEN_BYVAL_ALIGN(elemlen, elembyval, elemalign, \
                                       range_elemlen, range_elembyval, range_elemalign) \
  do { \
    elmlen = elemlen; \
    elmbyval = elembyval; \
    elmalign = elemalign; \
    range_elmlen = range_elemlen; \
    range_elmbyval = range_elembyval; \
    range_elmalign = range_elemalign; \
  } while (0);

Status DocPgInit() {
  PG_RETURN_NOT_OK(YbgInit());
  return Status::OK();
}

//-----------------------------------------------------------------------------
// Types
//
// YBTODO(skumar@yugabyte.com) This work on TYPE need works.
// - Postgres has added some new type macros and removed others. I removed the
//   obsolete types, but new ones need to be introduced here.
// - Access to Postgres macros such as TEXTOID should be coded in src/postgres
//   layer. Docdb would then make callback to Postgres. That's how we've done
//   in our code. Why does this module work directly with Postgres macros?
// I noted that nobody in SQL frontend team reviewed this work in the past.
// In the future, please discuss with them when calling or using Posgres API.
//-----------------------------------------------------------------------------

class DocPgTypeAnalyzer {
 public:
  const YBCPgTypeEntity* GetTypeEntity(int32_t type_oid) {
    const auto iter = type_map_.find(type_oid);
    if (iter != type_map_.end()) {
      return iter->second;
    }
    LOG(INFO) << "Could not find type entity for oid " << type_oid;
    return nullptr;
  }

 private:
  DocPgTypeAnalyzer() {
    // Setup type mapping.
    const YBCPgTypeEntity *type_table;
    int count;

    YbgGetTypeTable(&type_table, &count);
    for (int idx = 0; idx < count; idx++) {
        const YBCPgTypeEntity *type_entity = &type_table[idx];
        type_map_[type_entity->type_oid] = type_entity;
    }
  }

  // Mapping table of YugaByte and PostgreSQL datatypes.
  std::unordered_map<int, const YBCPgTypeEntity *> type_map_;

  friend class Singleton<DocPgTypeAnalyzer>;
  DISALLOW_COPY_AND_ASSIGN(DocPgTypeAnalyzer);
};

//-----------------------------------------------------------------------------
// Expressions/Values
//-----------------------------------------------------------------------------

const YBCPgTypeEntity* DocPgGetTypeEntity(YbgTypeDesc pg_type) {
    return Singleton<DocPgTypeAnalyzer>::get()->GetTypeEntity(pg_type.type_id);
}

Status DocPgAddVarRef(size_t column_idx,
                      int32_t attno,
                      int32_t typid,
                      int32_t typmod,
                      int32_t collid,
                      std::map<int, const DocPgVarRef> *var_map) {
  if (var_map->find(attno) != var_map->end()) {
    VLOG(1) << "Attribute " << attno << " is already processed";
    return Status::OK();
  }
  var_map->emplace(std::piecewise_construct,
                   std::tuple(attno),
                   std::tuple(DocPgVarRef {
                     .var_col_idx = column_idx,
                     .var_type = DocPgGetTypeEntity({typid, typmod}),
                     .var_type_attrs = {typmod},
                   }));
  VLOG(1) << "Attribute " << attno << " has been processed";
  return Status::OK();
}

Status DocPgPrepareExpr(const std::string& expr_str,
                        YbgPreparedExpr *expr,
                        DocPgVarRef *ret_type) {
  char *expr_cstring = const_cast<char *>(expr_str.c_str());
  VLOG(1) << "Deserialize " << expr_cstring;
  PG_RETURN_NOT_OK(YbgPrepareExpr(expr_cstring, expr));
  if (ret_type != nullptr) {
    int32_t typid;
    int32_t typmod;
    PG_RETURN_NOT_OK(YbgExprType(*expr, &typid));
    PG_RETURN_NOT_OK(YbgExprTypmod(*expr, &typmod));
    *ret_type = DocPgVarRef {
      .var_col_idx = dockv::ReaderProjection::kNotFoundIndex,
      .var_type = DocPgGetTypeEntity({typid, typmod}),
      .var_type_attrs = {typmod},
    };
    VLOG(1) << "Processed expression return type";
  }
  return Status::OK();
}

Status DocPgCreateExprCtx(const std::map<int, const DocPgVarRef>& var_map,
                          YbgExprContext *expr_ctx) {
  if (var_map.empty()) {
    return Status::OK();
  }

  int32_t min_attno = var_map.begin()->first;
  int32_t max_attno = var_map.rbegin()->first;

  VLOG(2) << "Allocating expr context: (" << min_attno << ", " << max_attno << ")";
  PG_RETURN_NOT_OK(YbgExprContextCreate(min_attno, max_attno, expr_ctx));
  return Status::OK();
}

// Wrapper for PgValueToDatum to safely call from the YbGate.
// The PgValueToDatum function was initially designed to be used in the PgGate where it converts
// values coming from DocDB into Postgres format. The PgGate runs within Postgres where exception
// handling is available. YbGate runs within DocDB, so it requires PG_SETUP_ERROR_REPORTING macro.
// The PG_SETUP_ERROR_REPORTING requires the surrounding function to return YbgStatus,
// hence the wrapper.
YbgStatus PgValueToDatumHelper(const YBCPgTypeEntity *type_entity,
                               YBCPgTypeAttrs type_attrs,
                               const dockv::PgValue& value,
                               uint64_t* datum) {
  PG_SETUP_ERROR_REPORTING();
  Status s = PgValueToDatum(type_entity, type_attrs, value, datum);
  if (!s.ok()) {
    return YbgStatusCreateError(s.message().cdata(), __FILE__, __LINE__);
  }
  PG_STATUS_OK();
}

Status DocPgPrepareExprCtx(const dockv::PgTableRow& table_row,
                           const std::map<int, const DocPgVarRef>& var_map,
                           YbgExprContext expr_ctx) {
  PG_RETURN_NOT_OK(YbgExprContextReset(expr_ctx));
  // Set the column values (used to resolve scan variables in the expression).
  for (const auto& [attno, arg_ref] : var_map) {
    auto val = table_row.GetValueByIndex(arg_ref.var_col_idx);
    uint64_t datum = 0;
    const bool is_null = !val;
    if (!is_null) {
      PG_RETURN_NOT_OK(PgValueToDatumHelper(
          arg_ref.var_type, arg_ref.var_type_attrs, *val, &datum));
    }
    VLOG(1) << "Adding value for attno " << attno;
    PG_RETURN_NOT_OK(YbgExprContextAddColValue(expr_ctx, attno, datum, is_null));
  }
  return Status::OK();
}

Result<std::pair<uint64_t, bool>> DocPgEvalExpr(YbgPreparedExpr expr, YbgExprContext expr_ctx) {
  uint64_t datum = {};
  bool is_null = false;
  PG_RETURN_NOT_OK(YbgEvalExpr(expr, expr_ctx, &datum, &is_null));
  return std::make_pair(datum, is_null);
}

Status SetValueFromQLBinary(
    const QLValuePB ql_value, const int pg_data_type,
    const std::unordered_map<uint32_t, string> &enum_oid_label_map,
    const std::unordered_map<uint32_t, std::vector<master::PgAttributePB>> &composite_atts_map,
    DatumMessagePB *cdc_datum_message) {
  PG_RETURN_NOT_OK(YbgPrepareMemoryContext());

  RETURN_NOT_OK(SetValueFromQLBinaryHelper(
      ql_value, pg_data_type, enum_oid_label_map, composite_atts_map, cdc_datum_message));
  PG_RETURN_NOT_OK(YbgResetMemoryContext());
  return Status::OK();
}

void DeleteMemoryContextIfSet() {
  if (YBCPgGetThreadLocalCurrentMemoryContext() != nullptr) {
    YbgDeleteMemoryContext();
  }
}

namespace {

// Given a 'ql_value', interpret the binary value in it as an array of type
// 'array_type' with elements of type 'elem_type' and store the individual
// elements in 'result'. Here, 'array_type' and 'elem_type' are PG typoids
// corresponding to the required array and element types.
// This function expects that YbgPrepareMemoryContext was called by the caller of this function.
Result<std::vector<std::string>> ExtractVectorFromQLBinaryValueHelper(
    const QLValuePB& ql_value, const int array_type, const int elem_type) {
  const uint64_t size = ql_value.binary_value().size();
  char *val = const_cast<char *>(ql_value.binary_value().c_str());

  YbgTypeDesc pg_arg_type {array_type, -1 /* typmod */};
  const YBCPgTypeEntity *arg_type = DocPgGetTypeEntity(pg_arg_type);
  YBCPgTypeAttrs type_attrs {-1 /* typmod */};
  uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<uint8_t *>(val), size, &type_attrs);

  uint64_t *datum_elements;
  int num_elems = 0;
  PG_RETURN_NOT_OK(YbgSplitArrayDatum(datum, elem_type, &datum_elements, &num_elems));
  YbgTypeDesc elem_pg_arg_type {elem_type, -1 /* typmod */};
  const YBCPgTypeEntity *elem_arg_type = DocPgGetTypeEntity(elem_pg_arg_type);
  VLOG(4) << "Number of parsed elements: " << num_elems;
  ThreadSafeArena arena;
  std::vector<std::string> result;
  for (int i = 0; i < num_elems; ++i) {
    pggate::PgConstant value(&arena,
                             elem_arg_type,
                             false /* collate_is_valid_non_c */,
                             nullptr /* collation_sortkey */,
                             datum_elements[i], false /* isNull */);
    const auto& str_val = VERIFY_RESULT(value.Eval())->string_value();
    VLOG(4) << "Parsed value: " << str_val.ToBuffer();
    result.emplace_back(str_val.cdata(), str_val.size());
  }
  return result;
}

const char *tz = "GMT";

}  // namespace

Result<std::vector<std::string>> ExtractTextArrayFromQLBinaryValue(const QLValuePB& ql_value) {
  PG_RETURN_NOT_OK(YbgPrepareMemoryContext());

  auto result = ExtractVectorFromQLBinaryValueHelper(ql_value, TEXTARRAYOID, TEXTOID);
  PG_RETURN_NOT_OK(YbgResetMemoryContext());
  return result;
}

void set_string_value(
    uint64_t datum,
    const char *func_name,
    DatumMessagePB *cdc_datum_message,
    const char *timezone = nullptr) {
  char *decoded_str = nullptr;

  if (func_name == nullptr) {
    return;
  }

  if (timezone == nullptr)
    decoded_str = DecodeDatum(func_name, (uintptr_t)datum);
  else
    decoded_str = DecodeTZDatum(func_name, (uintptr_t)datum, timezone, true);

  cdc_datum_message->set_datum_string(decoded_str, strlen(decoded_str));
}

char *get_range_string_value(
    uint64_t datum, const int elem_type, const char *func_name, const char *timezone,
    const int type) {
  char *decoded_str = nullptr;
  int16 elmlen;
  bool elmbyval;
  char elmalign;
  bool from_YB = true;
  char option = '\0';

  switch (elem_type) {
    case INT4OID:
      SET_ELEM_LEN_BYVAL_ALIGN(sizeof(int32), true, 'i');
      break;

    case NUMERICOID:
      SET_ELEM_LEN_BYVAL_ALIGN(-1, false, 'i');
      break;

    case TIMESTAMPOID:
    case INT8OID:
      SET_ELEM_LEN_BYVAL_ALIGN(sizeof(int64), true, 'i');
      break;

    case TIMESTAMPTZOID:
      SET_ELEM_LEN_BYVAL_ALIGN_OPT(sizeof(int64), true, 'i', 't');
      break;

    case DATEOID:
      SET_ELEM_LEN_BYVAL_ALIGN(sizeof(int32), true, 'i');
      break;

    default:
      SET_ELEM_LEN_BYVAL_ALIGN(-1, false, 'i');
      break;
  }

  if (func_name != nullptr) {
    decoded_str = DecodeRangeDatum(
        "range_out", (uintptr_t)datum, elmlen, elmbyval, elmalign, option, from_YB, func_name, type,
        timezone);
  }
  return decoded_str;
}

char *get_array_string_value(
    uint64_t datum, const int elem_type, const char *func_name, const char *timezone) {
  char *decoded_str = nullptr;
  int16 elmlen;
  bool elmbyval;
  char elmalign;
  char elmdelim = ',';
  bool from_YB = true;
  char option = '\0';

  switch (elem_type) {
    case TEXTOID:
    case XMLOID:
    case BYTEAOID:
    case INT2VECTOROID:
    case CHAROID:
    case REGPROCOID:
    case TIDOID:
    case CIDROID:
    case OIDVECTOROID:
    case BPCHAROID:
    case VARCHAROID:
    case PATHOID:
    case ACLITEMOID:
    case INETOID:
    case NUMERICOID:
    case BITOID:
    case VARBITOID:
    case REGPROCEDUREOID:
    case REGOPEROID:
    case REGOPERATOROID:
    case REGCLASSOID:
    case REGTYPEOID:
    case REGROLEOID:
    case REGNAMESPACEOID:
    case TSVECTOROID:
    case GTSVECTOROID:
    case TSQUERYOID:
    case REGCONFIGOID:
    case REGDICTIONARYOID:
    case JSONOID:
    case JSONBOID:
    case TXID_SNAPSHOTOID:
    case RECORDOID:
    case POLYGONOID:
      SET_ELEM_LEN_BYVAL_ALIGN(-1, false, 'i');
      break;

    case LINEOID:
    case CIRCLEOID:
      SET_ELEM_LEN_BYVAL_ALIGN(24, false, 'd');
      break;

    case CASHOID:
    case INT8OID:
    case TIMESTAMPOID:
    case TIMEOID:
      SET_ELEM_LEN_BYVAL_ALIGN(sizeof(int64), true, 'd');
      break;

    case BOOLOID:
      SET_ELEM_LEN_BYVAL_ALIGN(sizeof(bool), true, 'c');
      break;

    case NAMEOID:
      SET_ELEM_LEN_BYVAL_ALIGN(64, false, 'c');
      break;

    case INT2OID:
      SET_ELEM_LEN_BYVAL_ALIGN(2, true, 's');
      break;

    case INT4OID:
    case DATEOID:
    case ANYOID:
      SET_ELEM_LEN_BYVAL_ALIGN(sizeof(int32), true, 'i');
      break;

    case OIDOID:
    case CIDOID:
    case FLOAT4OID:
      SET_ELEM_LEN_BYVAL_ALIGN(4, true, 'i');
      break;

    case XIDOID:
      SET_ELEM_LEN_BYVAL_ALIGN(16/*sizeof(TransactionId)*/, true, 'i');
      break;

    case POINTOID:
    case INTERVALOID:
      SET_ELEM_LEN_BYVAL_ALIGN(16, false, 'd');
      break;

    case LSEGOID:
      SET_ELEM_LEN_BYVAL_ALIGN(32, false, 'd');
      break;

    case BOXOID:
      SET_ELEM_LEN_BYVAL_ALIGN(32, false, 'd');
      elmdelim = ';';
      break;

    case FLOAT8OID:
      SET_ELEM_LEN_BYVAL_ALIGN(8, true, 'd');
      break;

    case MACADDROID:
      SET_ELEM_LEN_BYVAL_ALIGN(6, false, 'i');
      break;

    case MACADDR8OID:
      SET_ELEM_LEN_BYVAL_ALIGN(8, false, 'i');
      break;

    case CSTRINGOID:
      SET_ELEM_LEN_BYVAL_ALIGN(-1, false, 'c');
      break;

    case TIMESTAMPTZOID:
      SET_ELEM_LEN_BYVAL_ALIGN_OPT(sizeof(int64), true, 'd', 't');
      break;

    case TIMETZOID:
      SET_ELEM_LEN_BYVAL_ALIGN(12, false, 'd');
      break;

    case UUIDOID:
      SET_ELEM_LEN_BYVAL_ALIGN(16, false, 'c');
      break;

    case LSNOID:
      SET_ELEM_LEN_BYVAL_ALIGN(sizeof(uint64), true, 'i');
      break;

    case INT4RANGEOID:
    case NUMRANGEOID:
    case TSRANGEOID:
    case TSTZRANGEOID:
    case DATERANGEOID:
    case INT8RANGEOID:
      SET_ELEM_LEN_BYVAL_ALIGN_OPT(-1, false, 'i', 'r');
      break;

    default:
      SET_ELEM_LEN_BYVAL_ALIGN(-1, false, 'i');
      break;
  }

  if (func_name != nullptr) {
    decoded_str = DecodeArrayDatum(
        "array_out", (uintptr_t)datum, elmlen, elmbyval, elmalign, elmdelim, from_YB, func_name,
        timezone, option);
  }
  return decoded_str;
}

char *get_range_array_string_value(
    uint64_t datum, const int elem_type, const char *func_name, const char *timezone) {
  char *decoded_str = nullptr;

  int16 elmlen, range_elmlen;
  bool elmbyval, range_elmbyval;
  char elmalign, range_elmalign;
  char elmdelim = ',';
  bool from_YB = true;
  char option = 'r';
  char range_option = '\0';

  switch (elem_type) {
    case INT4RANGEOID:
    case DATERANGEOID:
      SET_RANGE_ELEM_LEN_BYVAL_ALIGN(-1, false, 'i', sizeof(int32), true, 'i');
      break;

    case NUMRANGEOID:
      SET_RANGE_ELEM_LEN_BYVAL_ALIGN(-1, false, 'i', -1, false, 'i');
      break;

    case TSRANGEOID:
    case INT8RANGEOID:
      SET_RANGE_ELEM_LEN_BYVAL_ALIGN(-1, false, 'd', sizeof(int64), true, 'i');
      break;

    case TSTZRANGEOID:
      SET_RANGE_ELEM_LEN_BYVAL_ALIGN(-1, false, 'd', sizeof(int64), true, 'i');
      range_option = 't';
      break;

    default:
      SET_RANGE_ELEM_LEN_BYVAL_ALIGN(-1, false, 'i', -1, false, 'i');
      break;
  }

  if (func_name != nullptr) {
    decoded_str = DecodeRangeArrayDatum(
        "array_out", (uintptr_t)datum, elmlen, range_elmlen, elmbyval, range_elmbyval, elmalign,
        range_elmalign, elmdelim, option, range_option, from_YB, "range_out", func_name, elem_type,
        timezone);
  }
  return decoded_str;
}

void set_range_string_value(
    const QLValuePB ql_value,
    const YBCPgTypeEntity *arg_type,
    const int type_oid,
    char const *func_name,
    DatumMessagePB *cdc_datum_message,
    const char *timezone = nullptr) {
  YBCPgTypeAttrs type_attrs{-1 /* typmod */};
  string range_val = ql_value.binary_value();
  uint64_t size = range_val.size();
  char *val = const_cast<char *>(range_val.c_str());
  uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<uint8 *>(val), size, &type_attrs);

  char *decoded_str =
      get_range_string_value(datum, type_oid, func_name, timezone, arg_type->type_oid);
  cdc_datum_message->set_datum_string(decoded_str, strlen(decoded_str));
}

void set_array_string_value(
    const QLValuePB ql_value,
    const YBCPgTypeEntity *arg_type,
    const int type_oid,
    char const *func_name,
    DatumMessagePB *cdc_datum_message,
    const char *timezone = nullptr) {
  YBCPgTypeAttrs type_attrs{-1 /* typmod */};
  string vector_val = ql_value.binary_value();
  uint64_t size = vector_val.size();
  char *val = const_cast<char *>(vector_val.c_str());
  uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<uint8 *>(val), size, &type_attrs);
  char *decoded_str = get_array_string_value(datum, type_oid, func_name, timezone);
  cdc_datum_message->set_datum_string(decoded_str, strlen(decoded_str));
}

void set_range_array_string_value(
    const QLValuePB ql_value,
    const YBCPgTypeEntity *arg_type,
    const int type_oid,
    char const *func_name,
    DatumMessagePB *cdc_datum_message,
    const char *timezone = nullptr) {
  YBCPgTypeAttrs type_attrs{-1 /* typmod */};
  string arr_val = ql_value.binary_value();
  uint64_t size = arr_val.size();
  char *val = const_cast<char *>(arr_val.c_str());
  uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<uint8 *>(val), size, &type_attrs);
  char *decoded_str = get_range_array_string_value(datum, type_oid, func_name, timezone);
  cdc_datum_message->set_datum_string(decoded_str, strlen(decoded_str));
}

uint32_t get_array_element_type(uint32_t pg_data_type) {
  switch (pg_data_type) {
    case RECORDARRAYOID:
      return RECORDOID;
    case ANYARRAYOID:
      return ANYOID;
    case BOOLARRAYOID:
      return BOOLOID;
    case BYTEAARRAYOID:
      return BYTEAOID;
    case CHARARRAYOID:
      return CHAROID;
    case NAMEARRAYOID:
      return NAMEOID;
    case INT8ARRAYOID:
      return INT8OID;
    case INT2ARRAYOID:
      return INT2OID;
    case INT2VECTORARRAYOID:
      return INT2VECTOROID;
    case INT4ARRAYOID:
      return INT4OID;
    case REGPROCARRAYOID:
      return REGPROCOID;
    case TEXTARRAYOID:
      return TEXTOID;
    case OIDARRAYOID:
      return TEXTOID;
    case TIDARRAYOID:
      return TIDOID;
    case XIDARRAYOID:
      return XIDOID;
    case CIDARRAYOID:
      return CIDOID;
    case OIDVECTORARRAYOID:
      return OIDVECTOROID;
    case JSONARRAYOID:
      return JSONOID;
    case XMLARRAYOID:
      return XMLOID;
    case POINTARRAYOID:
      return POINTOID;
    case LSEGARRAYOID:
      return LSEGOID;
    case PATHARRAYOID:
      return PATHOID;
    case BOXARRAYOID:
      return BOXOID;
    case POLYGONARRAYOID:
      return POLYGONOID;
    case LINEARRAYOID:
      return LINEOID;
    case FLOAT4ARRAYOID:
      return FLOAT4OID;
    case FLOAT8ARRAYOID:
      return FLOAT8OID;
    case CIRCLEARRAYOID:
      return CIRCLEOID;
    case MACADDRARRAYOID:
      return MACADDROID;
    case INETARRAYOID:
      return INETOID;
    case CIDRARRAYOID:
      return CIDROID;
    case MACADDR8ARRAYOID:
      return MACADDR8OID;
    case ACLITEMARRAYOID:
      return ACLITEMOID;
    case BPCHARARRAYOID:
      return BPCHAROID;
    case VARCHARARRAYOID:
      return VARCHAROID;
    case DATEARRAYOID:
      return DATEOID;
    case TIMEARRAYOID:
      return TIMEOID;
    case TIMESTAMPARRAYOID:
      return TIMESTAMPOID;
    case TIMESTAMPTZARRAYOID:
      return TIMESTAMPTZOID;
    case INTERVALARRAYOID:
      return INTERVALOID;
    case TIMETZARRAYOID:
      return TIMETZOID;
    case BITARRAYOID:
      return BITOID;
    case VARBITARRAYOID:
      return VARBITOID;
    case NUMERICARRAYOID:
      return NUMERICOID;
    case REFCURSORARRAYOID:
      return REFCURSOROID;
    case REGPROCEDUREARRAYOID:
      return REGPROCEDUREOID;
    case REGOPERARRAYOID:
      return REGOPEROID;
    case REGOPERATORARRAYOID:
      return REGOPERATOROID;
    case REGCLASSARRAYOID:
      return REGCLASSOID;
    case REGTYPEARRAYOID:
      return REGTYPEOID;
    case REGROLEARRAYOID:
      return REGROLEOID;
    case REGNAMESPACEARRAYOID:
      return REGNAMESPACEOID;
    case UUIDARRAYOID:
      return UUIDOID;
    case TSVECTORARRAYOID:
      return TSVECTOROID;
    case GTSVECTORARRAYOID:
      return GTSVECTOROID;
    case TSQUERYARRAYOID:
      return TSQUERYOID;
    case REGCONFIGARRAYOID:
      return REGCONFIGOID;
    case REGDICTIONARYARRAYOID:
      return REGDICTIONARYOID;
    case JSONBARRAYOID:
      return JSONBOID;
    case JSONPATHARRAYOID:
      return JSONPATHOID;
    case TXID_SNAPSHOTARRAYOID:
      return TXID_SNAPSHOTOID;
    case INT4RANGEARRAYOID:
      return INT4RANGEOID;
    case NUMRANGEARRAYOID:
      return NUMRANGEOID;
    case TSRANGEARRAYOID:
      return TSRANGEOID;
    case TSTZRANGEARRAYOID:
      return TSTZRANGEOID;
    case DATERANGEARRAYOID:
      return TSTZRANGEOID;
    case INT8RANGEARRAYOID:
      return INT8RANGEOID;
    case CSTRINGARRAYOID:
      return CSTRINGOID;
    default:
      return kPgInvalidOid;
  }
}

uint32_t get_range_element_type(uint32_t pg_data_type) {
  switch (pg_data_type) {
    case INT4RANGEOID:
      return INT4OID;
    case NUMRANGEOID:
      return NUMERICOID;
    case TSRANGEOID:
      return TIMESTAMPOID;
    case TSTZRANGEOID:
      return TIMESTAMPTZOID;
    case DATERANGEOID:
      return DATEOID;
    case INT8RANGEOID:
      return INT8OID;
    default:
      return kPgInvalidOid;
  }
}

uint32_t get_range_array_element_type(uint32_t pg_data_type) {
  switch (pg_data_type) {
    case INT4RANGEARRAYOID:
      return INT4RANGEOID;
    case NUMRANGEARRAYOID:
      return NUMRANGEOID;
    case TSRANGEARRAYOID:
      return TSRANGEOID;
    case TSTZRANGEARRAYOID:
      return TSTZRANGEOID;
    case DATERANGEARRAYOID:
      return TSTZRANGEOID;
    case INT8RANGEARRAYOID:
      return INT8RANGEOID;
    default:
      return kPgInvalidOid;
  }
}

char *get_record_string_value(
    const std::unordered_map<uint32_t, std::vector<master::PgAttributePB>> &composite_atts_map,
    uint32_t type_id, uintptr_t datum) {
  const auto &att_pbs = composite_atts_map.at(type_id);
  size_t natts = att_pbs.size();
  PgAttributeRow *attrs[natts];
  for (size_t i = 0; i < natts; i++) {
    const auto &att_pb = att_pbs[i];
    PgAttributeRow *pg_att =
        reinterpret_cast<PgAttributeRow *>(malloc(sizeof(struct PgAttributeRow)));
    *pg_att = {att_pb.attrelid(),           "",
               att_pb.atttypid(),           att_pb.attstattarget(),
               (int16_t)att_pb.attlen(),    (int16_t)att_pb.attnum(),
               att_pb.attndims(),           att_pb.attcacheoff(),
               att_pb.atttypmod(),          att_pb.attbyval(),
               (int8_t)att_pb.attstorage(), (int8_t)att_pb.attalign(),
               att_pb.attnotnull(),         att_pb.atthasdef(),
               att_pb.atthasmissing(),      (int8_t)att_pb.attidentity(),
               att_pb.attisdropped(),       att_pb.attislocal(),
               att_pb.attinhcount(),        att_pb.attcollation()};
    strncpy(pg_att->attname, att_pb.attname().c_str(), sizeof(pg_att->attname));
    pg_att->attname[sizeof(pg_att->attname) - 1] = 0;
    attrs[i] = pg_att;
  }
  uintptr_t *values;
  bool *nulls;
  values = reinterpret_cast<uintptr_t *>(malloc(natts * sizeof(uintptr_t)));
  nulls = reinterpret_cast<bool *>(malloc(natts * sizeof(bool)));

  HeapDeformTuple(datum, attrs, natts, values, nulls);

  bool curr_att_modified = false;
  bool atts_modified = false;
  for (size_t i = 0; i < natts; i++) {
    curr_att_modified = false;
    const auto &att = attrs[i];
    if (composite_atts_map.find(att->atttypid) != composite_atts_map.end()) {
      values[i] = (uintptr_t)get_record_string_value(composite_atts_map, att->atttypid, values[i]);
      curr_att_modified = true;
    } else if (get_range_array_element_type(att->atttypid) != kPgInvalidOid) {
      auto elem_type = get_range_array_element_type(att->atttypid);
      if (elem_type == TSTZRANGEOID) {
        values[i] = (uintptr_t)get_range_array_string_value(
            values[i], elem_type, GetOutFuncName(att->atttypid), tz);
      } else {
        values[i] = (uintptr_t)get_range_array_string_value(
            values[i], elem_type, GetOutFuncName(att->atttypid), nullptr);
      }
      curr_att_modified = true;
    } else if (get_array_element_type(att->atttypid) != kPgInvalidOid) {
      auto elem_type = get_array_element_type(att->atttypid);
      if (elem_type == TIMESTAMPTZOID) {
        values[i] = (uintptr_t)get_array_string_value(
            values[i], elem_type, GetOutFuncName(att->atttypid), tz);
      } else {
        values[i] = (uintptr_t)get_array_string_value(
            values[i], elem_type, GetOutFuncName(att->atttypid), nullptr);
      }
      curr_att_modified = true;
    } else if (get_range_element_type(att->atttypid) != kPgInvalidOid) {
      auto elem_type = get_range_element_type(att->atttypid);
      if (elem_type == TIMESTAMPTZOID) {
        values[i] = (uintptr_t)get_range_string_value(
            values[i], elem_type, GetOutFuncName(att->atttypid), tz, att->atttypid);
      } else {
        values[i] = (uintptr_t)get_range_string_value(
            values[i], elem_type, GetOutFuncName(att->atttypid), nullptr, att->atttypid);
      }
      curr_att_modified = true;
    }

    if (curr_att_modified) {
      att->atttypid = CSTRINGOID;
      att->attalign = 'c';
      att->attstorage = 'p';
      att->attcollation = 0;
      att->attlen = -2;
      atts_modified = true;
    }
  }
  if (atts_modified) {
    datum = HeapFormTuple(attrs, natts, values, nulls);
  }

  auto decoded_string = DecodeRecordDatum(datum, attrs, natts);
  free(values);
  free(nulls);
  for (size_t i = 0; i < natts; i++) {
    free(attrs[i]);
  }
  return decoded_string;
}

// This function expects that YbgPrepareMemoryContext was called
// by the caller of this function.
Status SetValueFromQLBinaryHelper(
    const QLValuePB ql_value, const int pg_data_type,
    const std::unordered_map<uint32_t, string> &enum_oid_label_map,
    const std::unordered_map<uint32_t, std::vector<master::PgAttributePB>> &composite_atts_map,
    DatumMessagePB *cdc_datum_message) {
  uint64_t size;
  char *val;
  char const* func_name = nullptr;

  YbgTypeDesc pg_arg_type{pg_data_type, -1 /* typmod */};
  const YBCPgTypeEntity* arg_type = DocPgGetTypeEntity(pg_arg_type);

  YBCPgTypeAttrs type_attrs{-1 /* typmod */};

  cdc_datum_message->set_column_type(pg_data_type);
  switch (pg_data_type) {
    case BOOLOID: {
      func_name = "boolout";
      bool bool_val = ql_value.bool_value();
      cdc_datum_message->set_datum_bool(bool_val);
      break;
    }
    case BYTEAOID: {
      func_name = "byteaout";
      string bytea_val = ql_value.binary_value();
      size = bytea_val.size();
      val = const_cast<char *>(bytea_val.c_str());
      uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<void *>(val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case CHAROID: {
      func_name = "charout";
      int char_val = ql_value.int8_value();
      size = sizeof(int);
      uint64_t datum =
          arg_type->yb_to_datum(reinterpret_cast<char *>(&char_val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case NAMEOID: {
      func_name = "nameout";
      string name_val = ql_value.string_value();
      size = name_val.size();
      val = const_cast<char *>(name_val.c_str());
      uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<char *>(val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case INT8OID: {
      func_name = "int8out";
      int64_t int8_val = ql_value.int64_value();
      cdc_datum_message->set_datum_int64(int8_val);
      break;
    }
    case INT2OID: {
      func_name = "int2out";
      int int2_val = ql_value.int16_value();
      cdc_datum_message->set_datum_int32(int2_val);
      break;
    }
    case INT4OID: {
      func_name = "int4out";
      int int4_val = ql_value.int32_value();
      cdc_datum_message->set_datum_int32(int4_val);
      break;
    }
    case REGPROCOID: {
      func_name = "regprocout";
      int regproc_val = ql_value.uint32_value();
      size = arg_type->datum_fixed_size;
      uint64_t datum =
          arg_type->yb_to_datum(reinterpret_cast<uint32 *>(&regproc_val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case TEXTOID: {
      func_name = "textout";
      string text_val = ql_value.string_value();
      size = text_val.size();
      val = const_cast<char *>(text_val.c_str());
      uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<char *>(val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case OIDOID: {
      func_name = "oidout";
      uint32 oid_val = ql_value.uint32_value();
      cdc_datum_message->set_datum_int64(oid_val);
      break;
    }
    case TIDOID: {
      func_name = "tidout";
      string tid_val = ql_value.binary_value();
      size = arg_type->datum_fixed_size;
      val = const_cast<char *>(tid_val.c_str());
      uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<uint8 *>(val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case XIDOID: {
      func_name = "xidout";
      uint32 xid_val = ql_value.uint32_value();
      size = arg_type->datum_fixed_size;
      uint64_t datum =
          arg_type->yb_to_datum(reinterpret_cast<uint32 *>(&xid_val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case CIDOID: {
      func_name = "cidout";
      uint32 cid_val = ql_value.uint32_value();
      size = arg_type->datum_fixed_size;
      uint64_t datum =
          arg_type->yb_to_datum(reinterpret_cast<uint32 *>(&cid_val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case JSONOID: {
      func_name = "json_out";
      string json_val = ql_value.binary_value();
      size = json_val.size();
      val = const_cast<char *>(json_val.c_str());
      uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<uint8 *>(val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case XMLOID: {
      func_name = "xml_out";
      string xml_val = ql_value.binary_value();
      size = xml_val.size();
      val = const_cast<char *>(xml_val.c_str());
      uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<uint8 *>(val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case POINTOID: {
      func_name = "point_out";
      string point_val = ql_value.binary_value();
      size = arg_type->datum_fixed_size;
      val = const_cast<char *>(point_val.c_str());
      uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<uint8 *>(val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case LSEGOID: {
      func_name = "lseg_out";
      string lseg_val = ql_value.binary_value();
      size = arg_type->datum_fixed_size;
      val = const_cast<char *>(lseg_val.c_str());
      uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<int8 *>(val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case PATHOID: {
      func_name = "path_out";
      string path_val = ql_value.binary_value();
      size = path_val.size();
      val = const_cast<char *>(path_val.c_str());
      uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<uint8 *>(val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case BOXOID: {
      func_name = "box_out";
      string box_val = ql_value.binary_value();
      size = arg_type->datum_fixed_size;
      val = const_cast<char *>(box_val.c_str());
      uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<uint8 *>(val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case LINEOID: {
      func_name = "line_out";
      string line_val = ql_value.binary_value();
      size = arg_type->datum_fixed_size;
      val = const_cast<char *>(line_val.c_str());
      uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<uint8 *>(val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case FLOAT4OID: {
      func_name = "float4out";
      float float4_val = ql_value.float_value();
      cdc_datum_message->set_datum_float(float4_val);
      break;
    }
    case FLOAT8OID: {
      func_name = "float8out";
      double float8_val = ql_value.double_value();
      cdc_datum_message->set_datum_double(float8_val);
      break;
    }
    case CIRCLEOID: {
      func_name = "circle_out";
      string circle_val = ql_value.binary_value();
      size = arg_type->datum_fixed_size;
      val = const_cast<char *>(circle_val.c_str());
      uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<uint8 *>(val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case CASHOID: {
      func_name = "cash_out";
      int64_t cash_val = ql_value.int64_value();
      size = arg_type->datum_fixed_size;
      val = reinterpret_cast<char *>(&cash_val);
      uint64_t datum =
          arg_type->yb_to_datum(reinterpret_cast<int64 *>(&cash_val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case MACADDROID: {
      func_name = "macaddr_out";
      string macaddr_val = ql_value.binary_value();
      size = arg_type->datum_fixed_size;
      val = const_cast<char *>(macaddr_val.c_str());
      uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<uint8 *>(val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case INETOID: {
      func_name = "inet_out";
      string inet_val = ql_value.binary_value();
      size = inet_val.size();
      val = const_cast<char *>(inet_val.c_str());
      uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<uint8 *>(val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case CIDROID: {
      func_name = "cidr_out";
      string cidr_val = ql_value.binary_value();
      size = cidr_val.size();
      val = const_cast<char *>(cidr_val.c_str());
      uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<uint8 *>(val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case MACADDR8OID: {
      func_name = "macaddr8_out";
      string macaddr8_val = ql_value.binary_value();
      size = arg_type->datum_fixed_size;
      val = const_cast<char *>(macaddr8_val.c_str());
      uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<uint8 *>(val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case ACLITEMOID: {
      func_name = "aclitemout";
      string aclitem_val = ql_value.binary_value();
      size = arg_type->datum_fixed_size;
      val = const_cast<char *>(aclitem_val.c_str());
      uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<uint8 *>(val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case BPCHAROID: {
      func_name = "bpcharout";
      string bpchar_val = ql_value.string_value();
      size = bpchar_val.size();
      val = const_cast<char *>(bpchar_val.c_str());
      uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<char *>(val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case VARCHAROID: {
      func_name = "varcharout";
      string varchar_val = ql_value.string_value();
      size = varchar_val.size();
      val = const_cast<char *>(varchar_val.c_str());
      uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<char *>(val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case DATEOID: {
      func_name = "date_out";
      int date_val = ql_value.int32_value();
      size = arg_type->datum_fixed_size;
      uint64_t datum =
          arg_type->yb_to_datum(reinterpret_cast<int32 *>(&date_val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case TIMEOID: {
      func_name = "time_out";
      int64_t time_val = ql_value.int64_value();
      size = arg_type->datum_fixed_size;
      uint64_t datum =
          arg_type->yb_to_datum(reinterpret_cast<int64 *>(&time_val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case TIMESTAMPOID: {
      func_name = "timestamp_out";
      int64_t timestamp_val = ql_value.int64_value();
      size = arg_type->datum_fixed_size;
      uint64_t datum =
          arg_type->yb_to_datum(reinterpret_cast<int64 *>(&timestamp_val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case TIMESTAMPTZOID: {
      func_name = "timestamptz_out";
      int64_t timestamptz_val = ql_value.int64_value();
      size = arg_type->datum_fixed_size;
      uint64_t datum =
          arg_type->yb_to_datum(reinterpret_cast<int64 *>(&timestamptz_val), size, &type_attrs);

      set_string_value(datum, func_name, cdc_datum_message, tz);
      break;
    }
    case INTERVALOID: {
      func_name = "interval_out";
      string interval_val = ql_value.binary_value();
      size = arg_type->datum_fixed_size;
      val = const_cast<char *>(interval_val.c_str());
      uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<void *>(val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case TIMETZOID: {
      func_name = "timetz_out";
      string timetz_val = ql_value.binary_value();
      size = arg_type->datum_fixed_size;
      val = const_cast<char *>(timetz_val.c_str());
      uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<uint8 *>(val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case BITOID: {
      func_name = "bit_out";
      string bit_val = ql_value.binary_value();
      size = bit_val.size();
      val = const_cast<char *>(bit_val.c_str());
      uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<uint8 *>(val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case VARBITOID: {
      func_name = "varbit_out";
      string varbit_val = ql_value.binary_value();
      size = varbit_val.size();
      val = const_cast<char *>(varbit_val.c_str());
      uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<uint8 *>(val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case NUMERICOID: {
      func_name = "numeric_out";
      util::Decimal decimal;
      string string_val;

      RETURN_NOT_OK(decimal.DecodeFromComparable(ql_value.decimal_value()));
      RETURN_NOT_OK(decimal.ToPointString(&string_val, std::numeric_limits<int32_t>::max()));

      size = string_val.size();
      val = const_cast<char *>(string_val.c_str());
      uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<uint8_t *>(val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case REGPROCEDUREOID: {
      func_name = "regprocedureout";
      uint32 regprocedure_val = ql_value.uint32_value();
      size = arg_type->datum_fixed_size;
      uint64_t datum =
          arg_type->yb_to_datum(reinterpret_cast<uint32 *>(&regprocedure_val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case REGOPEROID: {
      func_name = "regoperout";
      uint32 regoper_val = ql_value.uint32_value();
      size = arg_type->datum_fixed_size;
      uint64_t datum =
          arg_type->yb_to_datum(reinterpret_cast<uint32 *>(&regoper_val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case REGOPERATOROID: {
      func_name = "regoperatorout";
      uint32 regoperator_val = ql_value.uint32_value();
      size = arg_type->datum_fixed_size;
      uint64_t datum =
          arg_type->yb_to_datum(reinterpret_cast<uint32 *>(&regoperator_val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case REGCLASSOID: {
      func_name = "regclassout";
      uint32 regclass_val = ql_value.uint32_value();
      size = arg_type->datum_fixed_size;
      uint64_t datum =
          arg_type->yb_to_datum(reinterpret_cast<uint32 *>(&regclass_val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case REGTYPEOID: {
      func_name = "regtypeout";
      uint32 regtype_val = ql_value.uint32_value();
      size = arg_type->datum_fixed_size;
      uint64_t datum =
          arg_type->yb_to_datum(reinterpret_cast<uint32 *>(&regtype_val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case REGROLEOID: {
      func_name = "regroleout";
      uint32 regrole_val = ql_value.uint32_value();
      size = arg_type->datum_fixed_size;
      uint64_t datum =
          arg_type->yb_to_datum(reinterpret_cast<uint32 *>(&regrole_val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case REGNAMESPACEOID: {
      func_name = "regnamespaceout";
      uint32 regnamespace_val = ql_value.uint32_value();
      size = arg_type->datum_fixed_size;
      uint64_t datum =
          arg_type->yb_to_datum(reinterpret_cast<uint32 *>(&regnamespace_val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case UUIDOID: {
      func_name = "uuid_out";
      string uuid_val = ql_value.binary_value();
      size = uuid_val.size();
      val = const_cast<char *>(uuid_val.c_str());
      uint64_t datum =
          arg_type->yb_to_datum(reinterpret_cast<unsigned char *>(val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case LSNOID: {
      func_name = "pg_lsn_out";
      uint64 pg_lsn_val = ql_value.uint64_value();
      size = arg_type->datum_fixed_size;
      uint64_t datum =
          arg_type->yb_to_datum(reinterpret_cast<uint64 *>(&pg_lsn_val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case TSQUERYOID: {
      func_name = "tsqueryout";
      string tsquery_val = ql_value.binary_value();
      size = tsquery_val.size();
      val = const_cast<char *>(tsquery_val.c_str());
      uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<uint8 *>(val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case REGCONFIGOID: {
      func_name = "regconfigout";
      uint32 regconfig_val = ql_value.uint32_value();
      size = arg_type->datum_fixed_size;
      uint64_t datum =
          arg_type->yb_to_datum(reinterpret_cast<uint32 *>(&regconfig_val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case REGDICTIONARYOID: {
      func_name = "regdictionaryout";
      uint32 regdictionary_val = ql_value.uint32_value();
      size = arg_type->datum_fixed_size;
      uint64_t datum =
          arg_type->yb_to_datum(reinterpret_cast<uint32 *>(&regdictionary_val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case JSONBOID: {
      func_name = "jsonb_out";
      string jsonb_val = ql_value.binary_value();
      size = jsonb_val.size();
      val = const_cast<char *>(jsonb_val.c_str());
      uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<void *>(val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case TXID_SNAPSHOTOID: {
      func_name = "pg_snapshot_out";
      string txid_val = ql_value.binary_value();
      size = txid_val.size();
      val = const_cast<char *>(txid_val.c_str());
      uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<void *>(val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case RECORDOID: {
      string record_val = ql_value.binary_value();
      size = record_val.size();
      val = const_cast<char *>(record_val.c_str());
      uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<uint8 *>(val), size, &type_attrs);
      uint32_t type_id = GetRecordTypeId((uintptr_t)datum);

      if (composite_atts_map.find(type_id) != composite_atts_map.end()) {
        char *decoded_str = get_record_string_value(composite_atts_map, type_id, (uintptr_t)datum);
        cdc_datum_message->set_datum_string(decoded_str, strlen(decoded_str));
      } else {
        LOG(INFO) << "For record of type : " << type_id << " no attributes found in the cache";
        return STATUS_SUBSTITUTE(CacheMissError, "composite");  // Do not change the message.
      }
      break;
    }
    case CSTRINGOID: {
      func_name = "cstring_out";
      string cstring_val = ql_value.string_value();
      size = cstring_val.size();
      val = const_cast<char *>(cstring_val.c_str());
      uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<char *>(val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case ANYOID: {
      func_name = "any_out";
      int any_val = ql_value.int32_value();
      size = arg_type->datum_fixed_size;
      uint64_t datum =
          arg_type->yb_to_datum(reinterpret_cast<int32 *>(&any_val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case VOIDOID: {
      func_name = "void_out";
      int64_t void_val = ql_value.int64_value();
      size = arg_type->datum_fixed_size;
      uint64_t datum =
          arg_type->yb_to_datum(reinterpret_cast<int64 *>(&void_val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case TRIGGEROID: {
      func_name = "trigger_out";
      uint32 trigger_val = ql_value.uint32_value();
      size = arg_type->datum_fixed_size;
      uint64_t datum =
          arg_type->yb_to_datum(reinterpret_cast<uint32 *>(&trigger_val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case LANGUAGE_HANDLEROID: {
      func_name = "language_handler_out";
      uint32 language_handler_val = ql_value.uint32_value();
      size = arg_type->datum_fixed_size;
      uint64_t datum = arg_type->yb_to_datum(
          reinterpret_cast<uint32 *>(&language_handler_val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case INTERNALOID: {
      func_name = "internal_out";
      int64_t internal_val = ql_value.int64_value();
      size = arg_type->datum_fixed_size;
      uint64_t datum =
          arg_type->yb_to_datum(reinterpret_cast<int64 *>(&internal_val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case ANYELEMENTOID: {
      func_name = "anyelement_out";
      int anyelement_val = ql_value.int32_value();
      size = arg_type->datum_fixed_size;
      uint64_t datum =
          arg_type->yb_to_datum(reinterpret_cast<int32 *>(&anyelement_val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case ANYNONARRAYOID: {
      func_name = "anynonarray_out";
      int anynonarray_val = ql_value.int32_value();
      size = arg_type->datum_fixed_size;
      uint64_t datum =
          arg_type->yb_to_datum(reinterpret_cast<int32 *>(&anynonarray_val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case ANYENUMOID: {
      int64_t yb_enum_oid = ql_value.int64_value();
      size = arg_type->datum_fixed_size;
      uint64_t enum_oid =
          arg_type->yb_to_datum(reinterpret_cast<int64 *>(&yb_enum_oid), size, &type_attrs);
      string label = "";
      if (enum_oid_label_map.find((uint32_t)enum_oid) != enum_oid_label_map.end()) {
        label = enum_oid_label_map.at((uint32_t)enum_oid);
        VLOG(1) << "For enum oid: " << enum_oid << " found label" << label;
      } else {
        LOG(INFO) << "For enum oid: " << enum_oid << " no label found in cache";
        return STATUS_SUBSTITUTE(CacheMissError, "enum");  // Do not change the message.
      }
      cdc_datum_message->set_datum_string(label.c_str(), strlen(label.c_str()));
      break;
    }
    case FDW_HANDLEROID: {
      func_name = "fdw_handler_out";
      uint32 fdw_handler_val = ql_value.uint32_value();
      size = arg_type->datum_fixed_size;
      uint64_t datum =
          arg_type->yb_to_datum(reinterpret_cast<uint32 *>(&fdw_handler_val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case INDEX_AM_HANDLEROID: {
      func_name = "index_am_handler_out";
      uint32 index_am_handler_val = ql_value.uint32_value();
      size = arg_type->datum_fixed_size;
      uint64_t datum = arg_type->yb_to_datum(
          reinterpret_cast<uint32 *>(&index_am_handler_val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case TSM_HANDLEROID: {
      func_name = "tsm_handler_out";
      uint32 tsm_handler_val = ql_value.uint32_value();
      size = arg_type->datum_fixed_size;
      uint64_t datum =
          arg_type->yb_to_datum(reinterpret_cast<uint32 *>(&tsm_handler_val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }
    case ANYRANGEOID: {
      func_name = "anyrange_out";
      string anyrange_val = ql_value.binary_value();
      size = anyrange_val.size();
      val = const_cast<char *>(anyrange_val.c_str());
      uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<uint8 *>(val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }

    case INT2VECTOROID: {
      func_name = "int2vectorout";
      string int2vector_val = ql_value.binary_value();
      size = int2vector_val.size();
      val = const_cast<char *>(int2vector_val.c_str());
      uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<uint8 *>(val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }

    case OIDVECTOROID: {
      func_name = "oidvectorout";
      string oidvector_val = ql_value.binary_value();
      size = oidvector_val.size();
      val = const_cast<char *>(oidvector_val.c_str());
      uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<uint8 *>(val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }

    case TSVECTOROID: {
      func_name = "tsvectorout";
      string tsvector_val = ql_value.binary_value();
      size = tsvector_val.size();
      val = const_cast<char *>(tsvector_val.c_str());
      uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<uint8 *>(val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }

    case GTSVECTOROID: {
      func_name = "gtsvectorout";
      string gtsvector_val = ql_value.binary_value();
      size = gtsvector_val.size();
      val = const_cast<char *>(gtsvector_val.c_str());
      uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<uint8 *>(val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }

    case POLYGONOID: {
      func_name = "poly_out";
      string polygon_val = ql_value.binary_value();
      size = polygon_val.size();
      val = const_cast<char *>(polygon_val.c_str());
      uint64_t datum = arg_type->yb_to_datum(reinterpret_cast<uint8 *>(val), size, &type_attrs);
      set_string_value(datum, func_name, cdc_datum_message);
      break;
    }

    // Range types
    case INT4RANGEOID: {
      func_name = "int4out";
      set_range_string_value(ql_value, arg_type, INT4OID, func_name, cdc_datum_message);
      break;
    }

    case NUMRANGEOID: {
      func_name = "numeric_out";
      set_range_string_value(ql_value, arg_type, NUMERICOID, func_name, cdc_datum_message);
      break;
    }

    case TSRANGEOID: {
      func_name = "timestamp_out";
      set_range_string_value(ql_value, arg_type, TIMESTAMPOID, func_name, cdc_datum_message);
      break;
    }

    case TSTZRANGEOID: {
      func_name = "timestamptz_out";
      set_range_string_value(ql_value, arg_type, TIMESTAMPTZOID, func_name, cdc_datum_message, tz);
      break;
    }

    case DATERANGEOID: {
      func_name = "date_out";
      set_range_string_value(ql_value, arg_type, DATEOID, func_name, cdc_datum_message);
      break;
    }

    case INT8RANGEOID: {
      func_name = "int8out";
      set_range_string_value(ql_value, arg_type, INT8OID, func_name, cdc_datum_message);
      break;
    }

    // Array types
    case XMLARRAYOID: {
      func_name = "xml_out";
      set_array_string_value(ql_value, arg_type, XMLOID, func_name, cdc_datum_message);
      break;
    }

    case LINEARRAYOID: {
      func_name = "line_out";
      set_array_string_value(ql_value, arg_type, LINEOID, func_name, cdc_datum_message);
      break;
    }

    case CIRCLEARRAYOID: {
      func_name = "circle_out";
      set_array_string_value(ql_value, arg_type, CIRCLEOID, func_name, cdc_datum_message);
      break;
    }

    case MONEYARRAYOID: {
      func_name = "cash_out";
      set_array_string_value(ql_value, arg_type, CASHOID, func_name, cdc_datum_message);
      break;
    }

    case BOOLARRAYOID: {
      func_name = "boolout";
      set_array_string_value(ql_value, arg_type, BOOLOID, func_name, cdc_datum_message);
      break;
    }

    case BYTEAARRAYOID: {
      func_name = "byteaout";
      set_array_string_value(ql_value, arg_type, BYTEAOID, func_name, cdc_datum_message);
      break;
    }

    case CHARARRAYOID: {
      func_name = "charout";
      set_array_string_value(ql_value, arg_type, CHAROID, func_name, cdc_datum_message);
      break;
    }

    case NAMEARRAYOID: {
      func_name = "nameout";
      set_array_string_value(ql_value, arg_type, NAMEOID, func_name, cdc_datum_message);
      break;
    }

    case INT2ARRAYOID: {
      func_name = "int2out";
      set_array_string_value(ql_value, arg_type, INT2OID, func_name, cdc_datum_message);
      break;
    }

    case INT2VECTORARRAYOID: {
      func_name = "int2vectorout";
      set_array_string_value(ql_value, arg_type, INT2VECTOROID, func_name, cdc_datum_message);
      break;
    }

    case INT4ARRAYOID: {
      func_name = "int4out";
      set_array_string_value(ql_value, arg_type, INT4OID, func_name, cdc_datum_message);
      break;
    }

    case REGPROCARRAYOID: {
      func_name = "regprocout";
      set_array_string_value(ql_value, arg_type, REGPROCOID, func_name, cdc_datum_message);
      break;
    }

    case TEXTARRAYOID: {
      func_name = "textout";
      set_array_string_value(ql_value, arg_type, TEXTOID, func_name, cdc_datum_message);
      break;
    }

    case OIDARRAYOID: {
      func_name = "oidout";
      set_array_string_value(ql_value, arg_type, OIDOID, func_name, cdc_datum_message);
      break;
    }

    case CIDRARRAYOID: {
      func_name = "cidr_out";
      set_array_string_value(ql_value, arg_type, CIDROID, func_name, cdc_datum_message);
      break;
    }

    case TIDARRAYOID: {
      func_name = "tidout";
      set_array_string_value(ql_value, arg_type, TIDOID, func_name, cdc_datum_message);
      break;
    }

    case XIDARRAYOID: {
      func_name = "xidout";
      set_array_string_value(ql_value, arg_type, XIDOID, func_name, cdc_datum_message);
      break;
    }

    case CIDARRAYOID: {
      func_name = "cidout";
      set_array_string_value(ql_value, arg_type, CIDOID, func_name, cdc_datum_message);
      break;
    }

    case OIDVECTORARRAYOID: {
      func_name = "oidvectorout";
      set_array_string_value(ql_value, arg_type, OIDVECTOROID, func_name, cdc_datum_message);
      break;
    }

    case BPCHARARRAYOID: {
      func_name = "bpcharout";
      set_array_string_value(ql_value, arg_type, BPCHAROID, func_name, cdc_datum_message);
      break;
    }

    case VARCHARARRAYOID: {
      func_name = "varcharout";
      set_array_string_value(ql_value, arg_type, VARCHAROID, func_name, cdc_datum_message);
      break;
    }

    case INT8ARRAYOID: {
      func_name = "int8out";
      set_array_string_value(ql_value, arg_type, INT8OID, func_name, cdc_datum_message);
      break;
    }

    case POINTARRAYOID: {
      func_name = "point_out";
      set_array_string_value(ql_value, arg_type, POINTOID, func_name, cdc_datum_message);
      break;
    }

    case LSEGARRAYOID: {
      func_name = "lseg_out";
      set_array_string_value(ql_value, arg_type, LSEGOID, func_name, cdc_datum_message);
      break;
    }

    case PATHARRAYOID: {
      func_name = "path_out";
      set_array_string_value(ql_value, arg_type, PATHOID, func_name, cdc_datum_message);
      break;
    }

    case BOXARRAYOID: {
      func_name = "box_out";
      set_array_string_value(ql_value, arg_type, BOXOID, func_name, cdc_datum_message);
      break;
    }

    case FLOAT4ARRAYOID: {
      func_name = "float4out";
      set_array_string_value(ql_value, arg_type, FLOAT4OID, func_name, cdc_datum_message);
      break;
    }

    case FLOAT8ARRAYOID: {
      func_name = "float8out";
      set_array_string_value(ql_value, arg_type, FLOAT8OID, func_name, cdc_datum_message);
      break;
    }

    case ACLITEMARRAYOID: {
      func_name = "aclitemout";
      set_array_string_value(ql_value, arg_type, ACLITEMOID, func_name, cdc_datum_message);
      break;
    }

    case MACADDRARRAYOID: {
      func_name = "macaddr_out";
      set_array_string_value(ql_value, arg_type, MACADDROID, func_name, cdc_datum_message);
      break;
    }

    case MACADDR8ARRAYOID: {
      func_name = "macaddr8_out";
      set_array_string_value(ql_value, arg_type, MACADDR8OID, func_name, cdc_datum_message);
      break;
    }

    case INETARRAYOID: {
      func_name = "inet_out";
      set_array_string_value(ql_value, arg_type, INETOID, func_name, cdc_datum_message);
      break;
    }

    case CSTRINGARRAYOID: {
      func_name = "cstring_out";
      set_array_string_value(ql_value, arg_type, CSTRINGOID, func_name, cdc_datum_message);
      break;
    }

    case TIMESTAMPARRAYOID: {
      func_name = "timestamp_out";
      set_array_string_value(ql_value, arg_type, TIMESTAMPOID, func_name, cdc_datum_message);
      break;
    }

    case DATEARRAYOID: {
      func_name = "date_out";
      set_array_string_value(ql_value, arg_type, DATEOID, func_name, cdc_datum_message);
      break;
    }

    case TIMEARRAYOID: {
      func_name = "time_out";
      set_array_string_value(ql_value, arg_type, TIMEOID, func_name, cdc_datum_message);
      break;
    }

    case TIMESTAMPTZARRAYOID: {
      func_name = "timestamptz_out";
      set_array_string_value(ql_value, arg_type, TIMESTAMPTZOID, func_name, cdc_datum_message, tz);
      break;
    }

    case INTERVALARRAYOID: {
      func_name = "interval_out";
      set_array_string_value(ql_value, arg_type, INTERVALOID, func_name, cdc_datum_message);
      break;
    }

    case NUMERICARRAYOID: {
      func_name = "numeric_out";
      set_array_string_value(ql_value, arg_type, NUMERICOID, func_name, cdc_datum_message);
      break;
    }

    case TIMETZARRAYOID: {
      func_name = "timetz_out";
      set_array_string_value(ql_value, arg_type, TIMETZOID, func_name, cdc_datum_message);
      break;
    }

    case BITARRAYOID: {
      func_name = "bit_out";
      set_array_string_value(ql_value, arg_type, BITOID, func_name, cdc_datum_message);
      break;
    }

    case VARBITARRAYOID: {
      func_name = "varbit_out";
      set_array_string_value(ql_value, arg_type, VARBITOID, func_name, cdc_datum_message);
      break;
    }

    case REGPROCEDUREARRAYOID: {
      func_name = "regprocedureout";
      set_array_string_value(ql_value, arg_type, REGPROCEDUREOID, func_name, cdc_datum_message);
      break;
    }

    case REGOPERARRAYOID: {
      func_name = "regoperout";
      set_array_string_value(ql_value, arg_type, REGOPEROID, func_name, cdc_datum_message);
      break;
    }

    case REGOPERATORARRAYOID: {
      func_name = "regoperatorout";
      set_array_string_value(ql_value, arg_type, REGOPERATOROID, func_name, cdc_datum_message);
      break;
    }

    case REGCLASSARRAYOID: {
      func_name = "regclassout";
      set_array_string_value(ql_value, arg_type, REGCLASSOID, func_name, cdc_datum_message);
      break;
    }

    case REGTYPEARRAYOID: {
      func_name = "regtypeout";
      set_array_string_value(ql_value, arg_type, REGTYPEOID, func_name, cdc_datum_message);
      break;
    }

    case REGROLEARRAYOID: {
      func_name = "regroleout";
      set_array_string_value(ql_value, arg_type, REGROLEOID, func_name, cdc_datum_message);
      break;
    }

    case REGNAMESPACEARRAYOID: {
      func_name = "regnamespaceout";
      set_array_string_value(ql_value, arg_type, REGNAMESPACEOID, func_name, cdc_datum_message);
      break;
    }

    case UUIDARRAYOID: {
      func_name = "uuid_out";
      set_array_string_value(ql_value, arg_type, UUIDOID, func_name, cdc_datum_message);
      break;
    }

    case PG_LSNARRAYOID: {
      func_name = "pg_lsn_out";
      set_array_string_value(ql_value, arg_type, LSNOID, func_name, cdc_datum_message);
      break;
    }

    case TSVECTORARRAYOID: {
      func_name = "tsvectorout";
      set_array_string_value(ql_value, arg_type, TSVECTOROID, func_name, cdc_datum_message);
      break;
    }

    case GTSVECTORARRAYOID: {
      func_name = "gtsvectorout";
      set_array_string_value(ql_value, arg_type, GTSVECTOROID, func_name, cdc_datum_message);
      break;
    }

    case TSQUERYARRAYOID: {
      func_name = "tsqueryout";
      set_array_string_value(ql_value, arg_type, TSQUERYOID, func_name, cdc_datum_message);
      break;
    }

    case REGCONFIGARRAYOID: {
      func_name = "regconfigout";
      set_array_string_value(ql_value, arg_type, REGCONFIGOID, func_name, cdc_datum_message);
      break;
    }

    case REGDICTIONARYARRAYOID: {
      func_name = "regdictionaryout";
      set_array_string_value(ql_value, arg_type, REGDICTIONARYOID, func_name, cdc_datum_message);
      break;
    }

    case JSONARRAYOID: {
      func_name = "json_out";
      set_array_string_value(ql_value, arg_type, JSONOID, func_name, cdc_datum_message);
      break;
    }

    case JSONBARRAYOID: {
      func_name = "jsonb_out";
      set_array_string_value(ql_value, arg_type, JSONBOID, func_name, cdc_datum_message);
      break;
    }

    case TXID_SNAPSHOTARRAYOID: {
      func_name = "pg_snapshot_out";
      set_array_string_value(ql_value, arg_type, TXID_SNAPSHOTOID, func_name, cdc_datum_message);
      break;
    }

    case RECORDARRAYOID: {
      cdc_datum_message->set_datum_string("");
      break;
    }

    case ANYARRAYOID: {
      func_name = "any_out";
      set_array_string_value(ql_value, arg_type, ANYOID, func_name, cdc_datum_message);
      break;
    }

    case POLYGONARRAYOID: {
      func_name = "poly_out";
      set_array_string_value(ql_value, arg_type, POLYGONOID, func_name, cdc_datum_message);
      break;
    }

    case INT4RANGEARRAYOID: {
      func_name = "int4out";
      set_range_array_string_value(ql_value, arg_type, INT4RANGEOID, func_name, cdc_datum_message);
      break;
    }

    case NUMRANGEARRAYOID: {
      func_name = "numeric_out";
      set_range_array_string_value(ql_value, arg_type, NUMRANGEOID, func_name, cdc_datum_message);
      break;
    }

    case TSRANGEARRAYOID: {
      func_name = "timestamp_out";
      set_range_array_string_value(ql_value, arg_type, TSRANGEOID, func_name, cdc_datum_message);
      break;
    }

    case TSTZRANGEARRAYOID: {
      func_name = "timestamptz_out";
      set_range_array_string_value(
          ql_value, arg_type, TSTZRANGEOID, func_name, cdc_datum_message, tz);
      break;
    }

    case DATERANGEARRAYOID: {
      func_name = "date_out";
      set_range_array_string_value(ql_value, arg_type, DATERANGEOID, func_name, cdc_datum_message);
      break;
    }

    case INT8RANGEARRAYOID: {
      func_name = "int8out";
      set_range_array_string_value(ql_value, arg_type, INT8RANGEOID, func_name, cdc_datum_message);
      break;
    }

    default:
      YB_LOG_EVERY_N_SECS(WARNING, 5)
          << Format(
                 "For column: $0 unsuppported pg_type_oid: $1 found in SetValueFromQLBinaryHelper",
                 cdc_datum_message->column_name(), pg_data_type)
          << THROTTLE_MSG;
      cdc_datum_message->set_datum_string("");
      break;
  }
  return Status::OK();
} // NOLINT(readability/fn_size)

}  // namespace yb::docdb
