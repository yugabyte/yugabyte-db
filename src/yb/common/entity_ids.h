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

#ifndef YB_COMMON_ENTITY_IDS_H
#define YB_COMMON_ENTITY_IDS_H

#include <string>
#include <set>
#include <utility>

namespace yb {

// TODO: switch many of these to opaque types for additional type safety and efficiency.

using TableId = std::string;
using TabletId = std::string;
using UDTypeId = std::string;

// TODO: keep only one of these.
using TabletServerId = std::string;
using TServerId = std::string;

using NamespaceId = std::string;
using TableName = std::string;
using UDTypeName = std::string;
using NamespaceName = std::string;
typedef std::pair<NamespaceId, TableName> NamespaceIdTableNamePair;
typedef std::set<NamespaceIdTableNamePair> SystemTableSet;

using RoleName = std::string;


}  // namespace yb

#endif  // YB_COMMON_ENTITY_IDS_H
