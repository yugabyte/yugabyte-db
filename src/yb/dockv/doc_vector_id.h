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

#pragma once

#include "yb/dockv/dockv_fwd.h"
#include "yb/dockv/key_bytes.h"

#include "yb/util/uuid.h"

#include "yb/vector_index/vector_index_fwd.h"


namespace yb::dockv {

struct EncodedDocVectorValue final {
  Slice data;
  Slice id;

  Result<vector_index::VectorId> DecodeId() const;

  static EncodedDocVectorValue FromSlice(Slice encoded);
};

class DocVectorValue final {
 public:
  explicit DocVectorValue(const QLValuePB& value, const vector_index::VectorId& id)
      : value_(value), id_(id)
  {}

  void EncodeTo(std::string* out) const;
  void EncodeTo(ValueBuffer* out) const;

  size_t EncodedSize() const;

  const QLValuePB& value() const {
    return value_;
  }

  static Slice SanitizeValue(Slice encoded);

  std::string ToString() const;

 private:
  template <typename Buffer>
  void DoEncodeTo(Buffer* buffer) const;

  const QLValuePB& value_;
  vector_index::VectorId id_;
};

bool IsNull(const DocVectorValue& v);

KeyBytes DocVectorKey(vector_index::VectorId vector_id);
std::array<Slice, 3> DocVectorKeyAsParts(Slice id, Slice encoded_write_time);

Status DecodeDocVectorKey(Slice* input, vector_index::VectorId* vector_id);
Result<vector_index::VectorId> DecodeDocVectorKey(Slice* input);

std::string DocVectorIdToString(const Uuid& vector_id);
std::string DocVectorIdToString(const vector_index::VectorId& vector_id);

std::string DocVectorKeyToString(const vector_index::VectorId& vector_id);
std::string DocVectorKeyToString(const vector_index::VectorId& vector_id, const DocHybridTime& ht);

} // namespace yb::dockv
