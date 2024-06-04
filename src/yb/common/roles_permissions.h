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

#pragma once

#include <bitset>
#include <string>

#include "yb/common/common_types.pb.h"

namespace yb {

static constexpr size_t kMaxPermissions = 7;
typedef std::bitset<kMaxPermissions> Permissions;

constexpr const char* const kRolesDataResource = "data";
constexpr const char* const kRolesRoleResource = "roles";

const std::vector<PermissionType>& all_permissions_for_resource(ResourceType resource_type);

// Return false if the permission is not supported by the given resource type.
bool valid_permission_for_resource(PermissionType permission, ResourceType resource_type);

std::string PermissionName(PermissionType permission);

std::string get_canonical_keyspace(const std::string &keyspace);
std::string get_canonical_table(const std::string &keyspace, const std::string &table);
std::string get_canonical_role(const std::string &role);
} // namespace yb
