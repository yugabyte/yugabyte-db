/*
 * Copyright (c) YugaByte, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// ListPermissions fetches list of permissions
func (a *AuthAPIClient) ListPermissions() (
	ybaclient.RBACManagementApiApiListPermissionsRequest,
) {
	return a.APIClient.RBACManagementApi.ListPermissions(a.ctx, a.CustomerUUID)
}

// GetRole fetches role
func (a *AuthAPIClient) GetRole(rUUID string) (
	ybaclient.RBACManagementApiApiGetRoleRequest,
) {
	return a.APIClient.RBACManagementApi.GetRole(a.ctx, a.CustomerUUID, rUUID)
}

// CreateRole creates role
func (a *AuthAPIClient) CreateRole() (
	ybaclient.RBACManagementApiApiCreateRoleRequest,
) {
	return a.APIClient.RBACManagementApi.CreateRole(a.ctx, a.CustomerUUID)
}

// DeleteRole deletes role
func (a *AuthAPIClient) DeleteRole(rUUID string) (
	ybaclient.RBACManagementApiApiDeleteRoleRequest,
) {
	return a.APIClient.RBACManagementApi.DeleteRole(a.ctx, a.CustomerUUID, rUUID)
}

// EditRole edits role
func (a *AuthAPIClient) EditRole(rUUID string) (
	ybaclient.RBACManagementApiApiEditRoleRequest,
) {
	return a.APIClient.RBACManagementApi.EditRole(a.ctx, a.CustomerUUID, rUUID)
}

// ListRoles fetches list of roles
func (a *AuthAPIClient) ListRoles() (
	ybaclient.RBACManagementApiApiListRolesRequest,
) {
	return a.APIClient.RBACManagementApi.ListRoles(a.ctx, a.CustomerUUID)
}

// SetRoleBinding sets role binding
func (a *AuthAPIClient) SetRoleBinding(userUUID string) (
	ybaclient.RBACManagementApiApiSetRoleBindingRequest,
) {
	return a.APIClient.RBACManagementApi.SetRoleBinding(a.ctx, a.CustomerUUID, userUUID)
}

// GetRoleBindings fetches role bindings
func (a *AuthAPIClient) GetRoleBindings() (
	ybaclient.RBACManagementApiApiGetRoleBindingsRequest,
) {
	return a.APIClient.RBACManagementApi.GetRoleBindings(a.ctx, a.CustomerUUID)
}
