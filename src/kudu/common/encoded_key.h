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
#ifndef KUDU_COMMON_ENCODED_KEY_H
#define KUDU_COMMON_ENCODED_KEY_H

#include <string>
#include <vector>

#include "kudu/common/schema.h"
#include "kudu/util/faststring.h"

namespace kudu {

class ConstContiguousRow;

class EncodedKey {
 public:
  // Constructs a new EncodedKey.
  // This class takes over the value of 'data' and contents of
  // raw_keys. Note that num_key_cols is the number of key columns for
  // the schema, but this may be different from the size of raw_keys
  // in which case raw_keys represents the supplied prefix of a
  // composite key.
  EncodedKey(faststring *data,
             vector<const void *> *raw_keys,
             size_t num_key_cols);

  static gscoped_ptr<EncodedKey> FromContiguousRow(const ConstContiguousRow& row);

  // Decode the encoded key specified in 'encoded', which must correspond to the
  // provided schema.
  // The returned row data is allocated from 'arena' and returned in '*result'.
  // If allocation fails or the encoding is invalid, returns a bad Status.
  static Status DecodeEncodedString(const Schema& schema,
                                    Arena* arena,
                                    const Slice& encoded,
                                    gscoped_ptr<EncodedKey> *result);

  // Given an EncodedKey, increment it to the next lexicographically greater EncodedKey.
  static Status IncrementEncodedKey(const Schema& tablet_schema,
                                    gscoped_ptr<EncodedKey>* key,
                                    Arena* arena);

  const Slice &encoded_key() const { return encoded_key_; }

  const vector<const void *> &raw_keys() const { return raw_keys_; }

  size_t num_key_columns() const { return num_key_cols_; }

  std::string Stringify(const Schema &schema) const;

  // Tests whether this EncodedKey is within the bounds given by 'start'
  // and 'end'.
  //
  // The empty bound has special significance: it's both the lowest value
  // (if in 'start') and the highest (if in 'end').
  bool InRange(const Slice& start, const Slice& end) const {
    return (start.compare(encoded_key_) <= 0 &&
            (end.empty() || encoded_key_.compare(end) < 0));
  }

  static std::string RangeToString(const EncodedKey* lower,
                                   const EncodedKey* upper);

  static std::string RangeToStringWithSchema(const EncodedKey* lower,
                                             const EncodedKey* upper,
                                             const Schema& schema);


 private:
  const int num_key_cols_;
  Slice encoded_key_;
  gscoped_ptr<uint8_t[]> data_;
  vector<const void *> raw_keys_;
};

// A builder for encoded key: creates an encoded key from
// one or more key columns specified as raw pointers.
class EncodedKeyBuilder {
 public:
  // 'schema' must remain valid for the lifetime of the EncodedKeyBuilder.
  explicit EncodedKeyBuilder(const Schema* schema);

  void Reset();

  void AddColumnKey(const void *raw_key);

  EncodedKey *BuildEncodedKey();

  void AssignCopy(const EncodedKeyBuilder &other);

 private:
  DISALLOW_COPY_AND_ASSIGN(EncodedKeyBuilder);

  const Schema* schema_;
  faststring encoded_key_;
  const size_t num_key_cols_;
  size_t idx_;
  vector<const void *> raw_keys_;
};

} // namespace kudu
#endif
