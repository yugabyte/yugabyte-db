// Copyright (c) YugaByte, Inc.

#ifndef YB_MASTER_MASTER_DEFAULTS_H
#define YB_MASTER_MASTER_DEFAULTS_H

#include <set>
#include <string>

namespace yb {
namespace master {

static const char* const kDefaultNamespaceName = "$$$_DEFAULT";
static const char* const kDefaultNamespaceId = "00000000000000000000000000000000";

static const char* const kSystemNamespaceName = "system";
static const char* const kSystemNamespaceId = "00000000000000000000000000000001";

static const char* const kSystemAuthNamespaceName = "system_auth";
static const char* const kSystemDistributedNamespaceName = "system_distributed";
static const char* const kSystemSchemaNamespaceName = "system_schema";
static const char* const kSystemTracesNamespaceName = "system_traces";

static const char* const kSystemPeersTableName = "peers";

// Needs to be updated each time we add a new system table.
static constexpr int kNumSystemTables = 1;

// The system tables which are supported by the master as virtual tables.
static const std::set<std::string> kMasterSupportedSystemTables = {
    std::string(kSystemPeersTableName) };

} // namespace master
} // namespace yb

#endif // YB_MASTER_MASTER_DEFAULTS_H
