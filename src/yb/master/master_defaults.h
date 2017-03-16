// Copyright (c) YugaByte, Inc.

#ifndef YB_MASTER_MASTER_DEFAULTS_H
#define YB_MASTER_MASTER_DEFAULTS_H

namespace yb {
namespace master {

static const char* const kDefaultNamespaceName = "$$$_DEFAULT";
static const char* const kDefaultNamespaceId = "00000000000000000000000000000000";

static const char* const kSystemNamespaceName = "system";
static const char* const kSystemNamespaceId = "00000000000000000000000000000001";

} // namespace master
} // namespace yb

#endif // YB_MASTER_MASTER_DEFAULTS_H
