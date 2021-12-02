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

#include "yb/common/types.h"

#include <memory>

#include "yb/gutil/singleton.h"

#include "yb/util/net/inetaddress.h"
#include "yb/util/status.h"
#include "yb/util/uuid.h"

using std::shared_ptr;
using std::unordered_map;

namespace yb {

template<typename TypeTraitsClass>
TypeInfo::TypeInfo(TypeTraitsClass t)
  : type_(TypeTraitsClass::type),
    physical_type_(TypeTraitsClass::physical_type),
    name_(TypeTraitsClass::name()),
    size_(TypeTraitsClass::size),
    min_value_(TypeTraitsClass::min_value()),
    append_func_(TypeTraitsClass::AppendDebugStringForValue),
    compare_func_(TypeTraitsClass::Compare) {
}

void TypeInfo::AppendDebugStringForValue(const void *ptr, string *str) const {
  append_func_(ptr, str);
}

int TypeInfo::Compare(const void *lhs, const void *rhs) const {
  return compare_func_(lhs, rhs);
}

class TypeInfoResolver {
 public:
  const TypeInfo* GetTypeInfo(DataType t) {
    const TypeInfo *type_info = mapping_[t].get();
    CHECK(type_info != nullptr) <<
      "Bad type: " << t;
    return type_info;
  }

 private:
  TypeInfoResolver() {
    AddMapping<UINT8>();
    AddMapping<INT8>();
    AddMapping<UINT16>();
    AddMapping<INT16>();
    AddMapping<UINT32>();
    AddMapping<INT32>();
    AddMapping<UINT64>();
    AddMapping<INT64>();
    AddMapping<VARINT>();
    AddMapping<TIMESTAMP>();
    AddMapping<DATE>();
    AddMapping<TIME>();
    AddMapping<STRING>();
    AddMapping<BOOL>();
    AddMapping<FLOAT>();
    AddMapping<DOUBLE>();
    AddMapping<BINARY>();
    AddMapping<INET>();
    AddMapping<JSONB>();
    AddMapping<MAP>();
    AddMapping<SET>();
    AddMapping<LIST>();
    AddMapping<DECIMAL>();
    AddMapping<UUID>();
    AddMapping<TIMEUUID>();
    AddMapping<USER_DEFINED_TYPE>();
    AddMapping<FROZEN>();
  }

  template<DataType type> void AddMapping() {
    TypeTraits<type> traits;
    mapping_.insert(make_pair(type, shared_ptr<TypeInfo>(new TypeInfo(traits))));
  }

  unordered_map<DataType,
                shared_ptr<const TypeInfo>,
                std::hash<size_t> > mapping_;

  friend class Singleton<TypeInfoResolver>;
  DISALLOW_COPY_AND_ASSIGN(TypeInfoResolver);
};

const TypeInfo* GetTypeInfo(DataType type) {
  return Singleton<TypeInfoResolver>::get()->GetTypeInfo(type);
}

void DataTypeTraits<INET>::AppendDebugStringForValue(const void *val, std::string *str) {
  const Slice *s = reinterpret_cast<const Slice *>(val);
  InetAddress addr;
  DCHECK(addr.FromSlice(*s).ok());
  str->append(addr.ToString());
}

void DataTypeTraits<UUID>::AppendDebugStringForValue(const void *val, std::string *str) {
  const Slice *s = reinterpret_cast<const Slice *>(val);
  Uuid uuid;
  DCHECK(uuid.FromSlice(*s).ok());
  str->append(uuid.ToString());
}

void DataTypeTraits<TIMEUUID>::AppendDebugStringForValue(const void *val, std::string *str) {
  const Slice *s = reinterpret_cast<const Slice *>(val);
  Uuid uuid;
  DCHECK(uuid.FromSlice(*s).ok());
  str->append(uuid.ToString());
}

} // namespace yb
