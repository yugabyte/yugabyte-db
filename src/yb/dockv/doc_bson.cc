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

#include "yb/dockv/doc_bson.h"

#include <bson/bson.h>

#include <cmath>

#include "yb/dockv/doc_kv_util.h"
#include "yb/util/result.h"

namespace yb::dockv {

namespace {

// Returns the canonical sort order for a BSON type, following MongoDB's ordering:
// MinKey < Null/Undefined < Number < String/Symbol < Document < Array < Binary <
// ObjectId < Boolean < DateTime < Timestamp < Regex < ... < MaxKey
//
// This matches the ordering used by the DocumentDB extension's GetSortOrderType().
int GetSortOrderType(bson_type_t type) {
  switch (type) {
    case BSON_TYPE_EOD:
    case BSON_TYPE_MINKEY:
      return 0x0;
    case BSON_TYPE_UNDEFINED:
    case BSON_TYPE_NULL:
      return 0x1;
    case BSON_TYPE_DOUBLE:
    case BSON_TYPE_INT32:
    case BSON_TYPE_INT64:
    case BSON_TYPE_DECIMAL128:
      return 0x2;
    case BSON_TYPE_BOOL:
      return 0x8;
    case BSON_TYPE_UTF8:
    case BSON_TYPE_SYMBOL:
      return 0x3;
    case BSON_TYPE_DOCUMENT:
      return 0x4;
    case BSON_TYPE_ARRAY:
      return 0x5;
    case BSON_TYPE_BINARY:
      return 0x6;
    case BSON_TYPE_OID:
      return 0x7;
    case BSON_TYPE_DATE_TIME:
      return 0x9;
    case BSON_TYPE_TIMESTAMP:
      return 0xA;
    case BSON_TYPE_REGEX:
      return 0xB;
    case BSON_TYPE_DBPOINTER:
      return 0xC;
    case BSON_TYPE_CODE:
      return 0xD;
    case BSON_TYPE_CODEWSCOPE:
      return 0xE;
    case BSON_TYPE_MAXKEY:
      return 0xF;
    default:
      return 0xFF;
  }
}

int CompareSortOrderType(bson_type_t left, bson_type_t right) {
  return GetSortOrderType(left) - GetSortOrderType(right);
}

// Converts a bson_value_t number to long double for cross-type comparison.
long double BsonNumberAsLongDouble(const bson_value_t* value) {
  switch (value->value_type) {
    case BSON_TYPE_DOUBLE:
      return static_cast<long double>(value->value.v_double);
    case BSON_TYPE_INT32:
      return static_cast<long double>(value->value.v_int32);
    case BSON_TYPE_INT64:
      return static_cast<long double>(value->value.v_int64);
    case BSON_TYPE_BOOL:
      return value->value.v_bool ? 1.0L : 0.0L;
    default:
      return 0.0L;
  }
}

// Converts a bson_value_t number to int64_t.
int64_t BsonNumberAsInt64(const bson_value_t* value) {
  switch (value->value_type) {
    case BSON_TYPE_INT64:
      return value->value.v_int64;
    case BSON_TYPE_INT32:
      return static_cast<int64_t>(value->value.v_int32);
    case BSON_TYPE_DOUBLE:
      return static_cast<int64_t>(value->value.v_double);
    case BSON_TYPE_BOOL:
      return value->value.v_bool ? 1 : 0;
    default:
      return 0;
  }
}

// Compares two BSON numeric values with cross-type promotion.
// Follows the same logic as DocumentDB's CompareNumbers.
int CompareNumbers(const bson_value_t* left, const bson_value_t* right) {
  // If either is double, compare as long double for precision.
  if (left->value_type == BSON_TYPE_DOUBLE || right->value_type == BSON_TYPE_DOUBLE) {
    long double left_val = BsonNumberAsLongDouble(left);
    long double right_val = BsonNumberAsLongDouble(right);

    if (std::isnan(left_val) && std::isnan(right_val)) return 0;
    if (std::isnan(left_val)) return -1;
    if (std::isnan(right_val)) return 1;

    return (left_val > right_val) ? 1 : (left_val < right_val) ? -1 : 0;
  }

  // Both are integer types (int32, int64, bool) - compare as int64.
  int64_t left_val = BsonNumberAsInt64(left);
  int64_t right_val = BsonNumberAsInt64(right);
  return (left_val > right_val) ? 1 : (left_val < right_val) ? -1 : 0;
}

int CompareStrings(const char* left, uint32_t left_len,
                   const char* right, uint32_t right_len) {
  uint32_t min_len = std::min(left_len, right_len);
  int cmp = memcmp(left, right, min_len);
  if (cmp != 0) return (cmp < 0) ? -1 : 1;
  return (left_len < right_len) ? -1 : (left_len > right_len) ? 1 : 0;
}

// Forward declaration for recursive comparison.
int CompareBsonIter(bson_iter_t* left, bson_iter_t* right, bool compare_fields);

// Compares two bson_value_t values that have the same sort order type.
int CompareBsonValues(const bson_value_t* left, const bson_value_t* right) {
  switch (left->value_type) {
    case BSON_TYPE_EOD:
    case BSON_TYPE_MINKEY:
    case BSON_TYPE_UNDEFINED:
    case BSON_TYPE_NULL:
    case BSON_TYPE_MAXKEY:
      return 0;

    case BSON_TYPE_DOUBLE:
    case BSON_TYPE_INT32:
    case BSON_TYPE_INT64:
    case BSON_TYPE_BOOL:
      return CompareNumbers(left, right);

    case BSON_TYPE_UTF8:
      return CompareStrings(
          left->value.v_utf8.str, left->value.v_utf8.len,
          right->value.v_utf8.str, right->value.v_utf8.len);

    case BSON_TYPE_SYMBOL:
      return CompareStrings(
          left->value.v_symbol.symbol, left->value.v_symbol.len,
          right->value.v_symbol.symbol, right->value.v_symbol.len);

    case BSON_TYPE_DOCUMENT:
    case BSON_TYPE_ARRAY: {
      bson_iter_t left_inner, right_inner;
      if (!bson_iter_init_from_data(
              &left_inner, left->value.v_doc.data, left->value.v_doc.data_len) ||
          !bson_iter_init_from_data(
              &right_inner, right->value.v_doc.data, right->value.v_doc.data_len)) {
        return 0;
      }
      return CompareBsonIter(&left_inner, &right_inner, /*compare_fields=*/true);
    }

    case BSON_TYPE_BINARY: {
      uint32_t left_len = left->value.v_binary.data_len;
      uint32_t right_len = right->value.v_binary.data_len;
      if (left_len != right_len) {
        return (left_len < right_len) ? -1 : 1;
      }
      if (left->value.v_binary.subtype != right->value.v_binary.subtype) {
        return (left->value.v_binary.subtype < right->value.v_binary.subtype) ? -1 : 1;
      }
      if (left_len > 0) {
        int cmp = memcmp(left->value.v_binary.data, right->value.v_binary.data, left_len);
        if (cmp != 0) return (cmp < 0) ? -1 : 1;
      }
      return 0;
    }

    case BSON_TYPE_OID:
      return bson_oid_compare(&left->value.v_oid, &right->value.v_oid);

    case BSON_TYPE_DATE_TIME: {
      int64_t l = left->value.v_datetime;
      int64_t r = right->value.v_datetime;
      return (l > r) ? 1 : (l < r) ? -1 : 0;
    }

    case BSON_TYPE_TIMESTAMP: {
      // Compare seconds first, then increment.
      if (left->value.v_timestamp.timestamp != right->value.v_timestamp.timestamp) {
        return (left->value.v_timestamp.timestamp > right->value.v_timestamp.timestamp)
            ? 1 : -1;
      }
      if (left->value.v_timestamp.increment != right->value.v_timestamp.increment) {
        return (left->value.v_timestamp.increment > right->value.v_timestamp.increment)
            ? 1 : -1;
      }
      return 0;
    }

    case BSON_TYPE_REGEX: {
      if (!left->value.v_regex.regex || !right->value.v_regex.regex) {
        return (left->value.v_regex.regex != nullptr) ? 1 : -1;
      }
      int cmp = strcmp(left->value.v_regex.regex, right->value.v_regex.regex);
      if (cmp != 0) return (cmp < 0) ? -1 : 1;
      if (!left->value.v_regex.options || !right->value.v_regex.options) {
        return (left->value.v_regex.options != nullptr) ? 1 : -1;
      }
      cmp = strcmp(left->value.v_regex.options, right->value.v_regex.options);
      return (cmp < 0) ? -1 : (cmp > 0) ? 1 : 0;
    }

    case BSON_TYPE_CODE:
      return CompareStrings(
          left->value.v_code.code, left->value.v_code.code_len,
          right->value.v_code.code, right->value.v_code.code_len);

    default:
      return 0;
  }
}

// Compares two BSON iterators element by element. This follows the same logic
// as the DocumentDB extension's CompareBsonIter.
int CompareBsonIter(bson_iter_t* left, bson_iter_t* right, bool compare_fields) {
  while (true) {
    bool left_next = bson_iter_next(left);
    bool right_next = bson_iter_next(right);

    if (!left_next && !right_next) return 0;
    if (!left_next || !right_next) return left_next ? 1 : -1;

    const bson_value_t* left_value = bson_iter_value(left);
    const bson_value_t* right_value = bson_iter_value(right);

    // Compare type sort order.
    int cmp = CompareSortOrderType(left_value->value_type, right_value->value_type);
    if (cmp != 0) return cmp;

    // Compare field names if requested.
    if (compare_fields) {
      const char* left_key = bson_iter_key(left);
      uint32_t left_key_len = bson_iter_key_len(left);
      const char* right_key = bson_iter_key(right);
      uint32_t right_key_len = bson_iter_key_len(right);
      cmp = CompareStrings(left_key, left_key_len, right_key, right_key_len);
      if (cmp != 0) return cmp;
    }

    // Compare values.
    cmp = CompareBsonValues(left_value, right_value);
    if (cmp != 0) return cmp;
  }
}

}  // namespace

int CompareBson(Slice a, Slice b) {
  bson_iter_t left_iter, right_iter;

  if (!bson_iter_init_from_data(
          &left_iter, reinterpret_cast<const uint8_t*>(a.data()), a.size()) ||
      !bson_iter_init_from_data(
          &right_iter, reinterpret_cast<const uint8_t*>(b.data()), b.size())) {
    // Invalid BSON data, fall back to byte-wise comparison.
    return a.compare(b);
  }

  return CompareBsonIter(&left_iter, &right_iter, /*compare_fields=*/true);
}

// YB_TODO: Currently mapping BSON keys to string encoding, which is not correct. These functions
// need to be updated, so that it gets converted such that the binary representation matches the
// BSON sort order.

void BsonKeyToComparableBinary(Slice slice, KeyBuffer& dest) {
  ZeroEncodeAndAppendStrToKey(slice, dest);
}

void BsonKeyToComparableBinaryDescending(Slice slice, KeyBuffer& dest) {
  ComplementZeroEncodeAndAppendStrToKey(slice, dest);
}

Status BsonKeyFromComparableBinary(Slice* slice, std::string* result) {
  return DecodeZeroEncodedStr(slice, result);
}

Result<const char*> BsonKeyFromComparableBinary(
    const char* begin, const char* end, ValueBuffer* out) {
  return DecodeZeroEncodedStr(begin, end, out);
}

Result<const char*> SkipComparableBson(const char* begin, const char* end) {
  return SkipZeroEncodedStr(begin, end);
}

Status BsonKeyFromComparableBinaryDescending(Slice* slice, std::string* result) {
  return DecodeComplementZeroEncodedStr(slice, result);
}

Result<const char*> BsonKeyFromComparableBinaryDescending(
    const char* begin, const char* end, ValueBuffer* out) {
  return DecodeComplementZeroEncodedStr(begin, end, out);
}

Result<const char*> SkipComparableBsonDescending(const char* begin, const char* end) {
  return SkipComplementZeroEncodedStr(begin, end);
}

}  // namespace yb::dockv
