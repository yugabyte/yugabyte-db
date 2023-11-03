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
//

#include "yb/common/common_types_util.h"

#include <array>
#include <utility>

#include "yb/util/logging.h"

using std::array;
using std::make_pair;
using std::pair;
using std::string;

namespace yb {

const char* DatabaseTypeName(YQLDatabase db) {
  switch (db) {
    case YQL_DATABASE_UNKNOWN: break;
    case YQL_DATABASE_CQL: return kDBTypeNameCql;
    case YQL_DATABASE_PGSQL: return kDBTypeNamePgsql;
    case YQL_DATABASE_REDIS: return kDBTypeNameRedis;
  }
  CHECK(false) << "Unexpected db type " << db;
  return kDBTypeNameUnknown;
}

YQLDatabase DatabaseTypeByName(const string& db_type_name) {
  static const array<pair<const char*, YQLDatabase>, 3> db_types{
    make_pair(kDBTypeNameCql, YQLDatabase::YQL_DATABASE_CQL),
    make_pair(kDBTypeNamePgsql, YQLDatabase::YQL_DATABASE_PGSQL),
    make_pair(kDBTypeNameRedis, YQLDatabase::YQL_DATABASE_REDIS)};
  for (const auto& db : db_types) {
    if (db_type_name == db.first) {
      return db.second;
    }
  }
  return YQLDatabase::YQL_DATABASE_UNKNOWN;
}

} // namespace yb
