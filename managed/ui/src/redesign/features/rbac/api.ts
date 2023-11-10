/*
 * Created on Thu Jul 13 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import axios from "axios";
import Cookies from 'js-cookie';
import { Role } from "./roles";
import { Permission, ResourceType } from "./permission";
import { RbacUser, RbacUserWithResources } from "./users/interface/Users";
import { ROOT_URL } from "../../../config";
import { mapResourceBindingsToApi } from "./rbacUtils";
import { RbacBindings } from "./users/components/UserUtils";
import { RunTimeConfigEntry } from "../universe/universe-form/utils/dto";


export const getAllAvailablePermissions = (resourceType?: ResourceType) => {
    const cUUID = localStorage.getItem('customerId');
    const requestUrl = `${ROOT_URL}/customers/${cUUID}/rbac/permissions`;
    return axios.get<Permission[]>(requestUrl);
};

export const createRole = (role: Role) => {
    const cUUID = localStorage.getItem('customerId');
    const requestUrl = `${ROOT_URL}/customers/${cUUID}/rbac/role`;
    return axios.post(requestUrl, {
        name: role.name,
        description: role.description,
        permissionList: role.permissionDetails.permissionList
    });
};

export const editRole = (role: Role) => {
    const cUUID = localStorage.getItem('customerId');
    const requestUrl = `${ROOT_URL}/customers/${cUUID}/rbac/role/${role.roleUUID}`;
    return axios.put(requestUrl, {
        description: role.description,
        permissionList: role.permissionDetails.permissionList
    });
};

export const getAllRoles = () => {
    const cUUID = localStorage.getItem('customerId');
    const requestUrl = `${ROOT_URL}/customers/${cUUID}/rbac/role`;
    return axios.get<Role[]>(requestUrl);
};

export const deleteRole = (role: Role) => {
    const cUUID = localStorage.getItem('customerId');
    const requestUrl = `${ROOT_URL}/customers/${cUUID}/rbac/role/${role.roleUUID}`;
    return axios.delete(requestUrl);
};

export const fetchUserPermissions = () => {
    const cUUID = localStorage.getItem('customerId');
    const userId = Cookies.get('userId') ?? localStorage.getItem('userId');
    const requestUrl = `${ROOT_URL}/customers/${cUUID}/rbac/user/${userId}`;
    return axios.get(requestUrl);
};

export const getAllUsers = () => {
    const cUUID = localStorage.getItem('customerId');
    const requestUrl = `${ROOT_URL}/customers/${cUUID}/users`;
    return axios.get<RbacUser[]>(requestUrl);
};

export const editUsersRolesBindings = (userUUID: string, usersWithRole: RbacUserWithResources) => {
    const cUUID = localStorage.getItem('customerId');
    const requestUrl = `${ROOT_URL}/customers/${cUUID}/rbac/role_binding/${userUUID}`;
    const resourceDefinitions = mapResourceBindingsToApi(usersWithRole);
    return axios.post(requestUrl, {
        roleResourceDefinitions: resourceDefinitions
    });
};

export const getRoleBindingsForUser = (userUUID: string) => {
    const cUUID = localStorage.getItem('customerId');
    const requestUrl = `${ROOT_URL}/customers/${cUUID}/rbac/role_binding?userUUID=${userUUID}`;
    return axios.get<RbacUserWithResources>(requestUrl);
};

export const getRoleBindingsForAllUsers = () => {
    const cUUID = localStorage.getItem('customerId');
    const requestUrl = `${ROOT_URL}/customers/${cUUID}/rbac/role_binding`;
    return axios.get<RbacBindings[]>(requestUrl);
};

export const createUser = (user: RbacUserWithResources) => {
    const cUUID = localStorage.getItem('customerId');
    const requestUrl = `${ROOT_URL}/customers/${cUUID}/users`;
    const resourceDefinitions = mapResourceBindingsToApi(user);
    return axios.post(requestUrl, {
        ...user,
        roleResourceDefinitions: resourceDefinitions
    });
};

export const deleteUser = (user: RbacUserWithResources) => {
    const cUUID = localStorage.getItem('customerId');
    return axios.delete(`${ROOT_URL}/customers/${cUUID}/users/${user.uuid}`);
};

export const getApiRoutePermMapList = () => {
    return axios.get(`${ROOT_URL}/rbac/routes`);
};

export const getRBACEnabledStatus = () => {
    return axios.get<RunTimeConfigEntry[]>(`${ROOT_URL}/runtime_config/feature_flags`);
};
