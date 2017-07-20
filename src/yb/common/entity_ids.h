// Copyright (c) YugaByte, Inc.

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
