// Copyright (c) YugaByte, Inc.

#ifndef YB_COMMON_ENTITY_IDS_H
#define YB_COMMON_ENTITY_IDS_H

#include <string>

namespace yb {

// TODO: switch many of these to opaque types for additional type safety and efficiency.

using TableId = std::string;
using TabletId = std::string;

// TODO: keep only one of these.
using TabletServerId = std::string;
using TServerId = std::string;

using NamespaceId = std::string;

}  // namespace yb

#endif  // YB_COMMON_ENTITY_IDS_H
