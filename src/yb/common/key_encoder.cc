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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/common/key_encoder.h"

#include <array>
#include <functional>
#include <string>

#include "yb/gutil/singleton.h"

#include "yb/util/faststring.h"

namespace yb {

// A resolver for Encoders
template<class Buffer>
class EncoderResolver {
 public:
  const KeyEncoder<Buffer>& GetKeyEncoder(DataType t) {
    return *encoders_[to_underlying(t)];
  }

  const bool HasKeyEncoderForType(DataType t) {
    return encoders_[to_underlying(t)] != nullptr;
  }

 private:
  EncoderResolver() {
    AddMapping<DataType::BOOL>();
    AddMapping<DataType::UINT8>();
    AddMapping<DataType::INT8>();
    AddMapping<DataType::UINT16>();
    AddMapping<DataType::INT16>();
    AddMapping<DataType::UINT32>();
    AddMapping<DataType::INT32>();
    AddMapping<DataType::UINT64>();
    AddMapping<DataType::INT64>();
    AddMapping<DataType::BINARY>();
    AddMapping<DataType::FLOAT>();
    AddMapping<DataType::DOUBLE>();
  }

  template<DataType Type>
  void AddMapping() {
    KeyEncoderTraits<Type, Buffer> traits;
    encoders_[to_underlying(Type)] = std::make_shared<KeyEncoder<Buffer>>(traits);
  }

  friend class Singleton<EncoderResolver<Buffer>>;
  std::array<std::shared_ptr<KeyEncoder<Buffer>>, kDataTypeMapSize> encoders_;
};

template <typename Buffer>
const KeyEncoder<Buffer>& GetKeyEncoder(const TypeInfo* typeinfo) {
  return Singleton<EncoderResolver<Buffer>>::get()->GetKeyEncoder(typeinfo->physical_type);
}

// Returns true if the type is allowed in keys.
bool IsTypeAllowableInKey(const TypeInfo* typeinfo) {
  return Singleton<EncoderResolver<faststring>>::get()->HasKeyEncoderForType(
      typeinfo->physical_type);
}

//------------------------------------------------------------
//// Template instantiations: We instantiate all possible templates to avoid linker issues.
//// see: https://isocpp.org/wiki/faq/templates#separate-template-fn-defn-from-decl
////------------------------------------------------------------

template
const KeyEncoder<std::string>& GetKeyEncoder(const TypeInfo* typeinfo);

template
const KeyEncoder<faststring>& GetKeyEncoder(const TypeInfo* typeinfo);

}  // namespace yb
