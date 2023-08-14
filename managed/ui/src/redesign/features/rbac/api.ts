/*
 * Created on Thu Jul 13 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import axios from "axios";
import { Permission, ResourceType } from "./permission";
import { IRole } from "./roles";
import { ROOT_URL } from "../../../config";


export const getAllAvailablePermissions = (resourceType?: ResourceType) => {
    const cUUID = localStorage.getItem('customerId');
    const requestUrl = `${ROOT_URL}/customers/${cUUID}/rbac/permissions`;
    return axios.get<Permission[]>(requestUrl);
};

export const createRole = (role: IRole) => {
    const cUUID = localStorage.getItem('customerId');
    const requestUrl = `${ROOT_URL}/customers/${cUUID}/rbac/role`;
    return axios.post(requestUrl, {
        name: role.name,
        permission_list: role.permissionDetails.permissionList
    });
};

export const editRole = (role: IRole ) => {
    const cUUID = localStorage.getItem('customerId');
    const requestUrl = `${ROOT_URL}/customers/${cUUID}/rbac/role/${role.roleUUID}`;
    return axios.put(requestUrl, {
        permission_list: role.permissionDetails.permissionList
    });
};

export const getAllRoles = () => {
    const cUUID = localStorage.getItem('customerId');
    const requestUrl = `${ROOT_URL}/customers/${cUUID}/rbac/role`;
    return axios.get<IRole[]>(requestUrl);
};

export const deleteRole = (role: IRole) => {
    const cUUID = localStorage.getItem('customerId');
    const requestUrl = `${ROOT_URL}/customers/${cUUID}/rbac/role/${role.roleUUID}`;
    return axios.delete(requestUrl);
};
