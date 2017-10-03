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

#include <boost/noncopyable.hpp>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "kudu/common/common.pb.h"
#include "kudu/common/key_encoder.h"
#include "kudu/gutil/map-util.h"
#include "kudu/gutil/singleton.h"
#include "kudu/util/faststring.h"

using std::shared_ptr;
using std::unordered_map;

namespace kudu {


// A resolver for Encoders
template <typename Buffer>
class EncoderResolver {
 public:
  const KeyEncoder<Buffer>& GetKeyEncoder(DataType t) {
    return *FindOrDie(encoders_, t);
  }

  const bool HasKeyEncoderForType(DataType t) {
    return ContainsKey(encoders_, t);
  }

 private:
  EncoderResolver<Buffer>() {
    AddMapping<UINT8>();
    AddMapping<INT8>();
    AddMapping<UINT16>();
    AddMapping<INT16>();
    AddMapping<UINT32>();
    AddMapping<INT32>();
    AddMapping<UINT64>();
    AddMapping<INT64>();
    AddMapping<BINARY>();
  }

  template<DataType Type> void AddMapping() {
    KeyEncoderTraits<Type, Buffer> traits;
    InsertOrDie(&encoders_, Type, shared_ptr<KeyEncoder<Buffer> >(new KeyEncoder<Buffer>(traits)));
  }

  friend class Singleton<EncoderResolver<Buffer> >;
  unordered_map<DataType, shared_ptr<KeyEncoder<Buffer> >, std::hash<size_t> > encoders_;
};

template <typename Buffer>
const KeyEncoder<Buffer>& GetKeyEncoder(const TypeInfo* typeinfo) {
  return Singleton<EncoderResolver<Buffer> >::get()->GetKeyEncoder(typeinfo->physical_type());
}

// Returns true if the type is allowed in keys.
const bool IsTypeAllowableInKey(const TypeInfo* typeinfo) {
  return Singleton<EncoderResolver<faststring> >::get()->HasKeyEncoderForType(
      typeinfo->physical_type());
}

//------------------------------------------------------------
//// Template instantiations: We instantiate all possible templates to avoid linker issues.
//// see: https://isocpp.org/wiki/faq/templates#separate-template-fn-defn-from-decl
////------------------------------------------------------------

template
const KeyEncoder<string>& GetKeyEncoder(const TypeInfo* typeinfo);

template
const KeyEncoder<faststring>& GetKeyEncoder(const TypeInfo* typeinfo);

}  // namespace kudu
