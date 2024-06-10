/*
 * Created on Mon Jul 17 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { find, groupBy, keys } from "lodash";
import { Permission, Resource, ResourceType } from "./permission";
import { UniverseResource } from "./policy/IPolicy";
import { RbacUserWithResources } from "./users/interface/Users";
import { Role, RoleType } from "./roles";

export const getPermissionDisplayText = (permission: Permission['prerequisitePermissions'][number]) => {
    return `${permission.resourceType}.${permission.action}`;
};

export const getResourceDefinitionSet = (role: Role, resourceType: UniverseResource['resourceType'], resource: UniverseResource) => {
    switch (resourceType) {
        case Resource.ROLE:
        case Resource.USER:
            return {
                ...resource,
                resourceType,
                allowAll: true,
                resourceUUIDSet: []
            };
        case Resource.DEFAULT:
            return {
                ...resource,
                resourceType,
                allowAll: false,
                resourceUUIDSet: [role.customerUUID]
            };
        case Resource.UNIVERSE:
            return {
                ...resource,
                resourceType,
                resourceUUIDSet: resource.allowAll ? [] : resource.resourceUUIDSet.map(i => i.universeUUID ?? i),
                allowAll: resource.allowAll
            };
    }

};

export const mapResourceBindingsToApi = (usersWithRole: RbacUserWithResources) => {
    return usersWithRole.roleResourceDefinitions?.map((res) => {

        const permissionGroups = groupBy(res.role?.permissionDetails.permissionList, "resourceType");
        if (res.roleType === RoleType.SYSTEM) {
            return {
                roleUUID: res.role?.roleUUID
            };
        }
        return {
            roleUUID: res.role?.roleUUID,
            resourceGroup: {
                resourceDefinitionSet: keys(permissionGroups).map((p) => getResourceDefinitionSet(res.role!, p as ResourceType, find(res.resourceGroup.resourceDefinitionSet, { resourceType: permissionGroups[p][0].resourceType })!))
            }
        };
    });
};
