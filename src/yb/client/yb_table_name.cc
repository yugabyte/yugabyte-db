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

#include "yb/client/yb_table_name.h"

#include <boost/functional/hash/hash.hpp>

#include "yb/master/master_defaults.h"
#include "yb/master/master_util.h"
#include "yb/master/master.pb.h"

namespace yb {
namespace client {

DEFINE_bool(yb_system_namespace_readonly, true, "Set system keyspace read-only.");

using std::string;

void YBTableName::SetIntoTableIdentifierPB(master::TableIdentifierPB* id) const {
  SetIntoNamespaceIdentifierPB(id->mutable_namespace_());
  id->set_table_name(table_name());
  if (!table_id_.empty()) {
    id->set_table_id(table_id_);
  } else {
    id->clear_table_id();
  }
}

void YBTableName::GetFromTableIdentifierPB(const master::TableIdentifierPB& id) {
  GetFromNamespaceIdentifierPB(id.namespace_());
  table_name_ = id.table_name();
  if (id.has_table_id()) {
    table_id_ = id.table_id();
  } else {
    table_id_.clear();
  }
}

void YBTableName::SetIntoNamespaceIdentifierPB(master::NamespaceIdentifierPB* id) const {
  if (!namespace_id_.empty()) {
    id->set_id(namespace_id_);
    if (!has_namespace()) {
      return;
    }
  } else {
    id->clear_id();
  }
  id->set_name(resolved_namespace_name());
}

void YBTableName::GetFromNamespaceIdentifierPB(const master::NamespaceIdentifierPB& id) {
  namespace_name_ = id.name();
  if (id.has_id()) {
    namespace_id_ = id.id();
  } else {
    namespace_id_.clear();
  }
}

bool YBTableName::is_system() const {
  return master::IsSystemNamespace(resolved_namespace_name());
}

size_t hash_value(const YBTableName& table_name) {
  size_t seed = 0;

  boost::hash_combine(seed, table_name.namespace_name());
  boost::hash_combine(seed, table_name.table_name());

  return seed;
}

} // namespace client
} // namespace yb
