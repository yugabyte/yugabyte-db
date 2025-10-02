/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// ListUsers fetches list of users associated with the customer
func (a *AuthAPIClient) ListUsers() ybaclient.UserManagementApiApiListUsersRequest {
	return a.APIClient.UserManagementApi.ListUsers(a.ctx, a.CustomerUUID)
}

// CreateUser creates a new user
func (a *AuthAPIClient) CreateUser() ybaclient.UserManagementApiApiCreateUserRequest {
	return a.APIClient.UserManagementApi.CreateUser(a.ctx, a.CustomerUUID)
}

// DeleteUser deletes a user
func (a *AuthAPIClient) DeleteUser(uUUID string) ybaclient.UserManagementApiApiDeleteUserRequest {
	return a.APIClient.UserManagementApi.DeleteUser(a.ctx, a.CustomerUUID, uUUID)
}

// GetUserDetails fetches user details
func (a *AuthAPIClient) GetUserDetails(
	uUUID string,
) ybaclient.UserManagementApiApiGetUserDetailsRequest {
	return a.APIClient.UserManagementApi.GetUserDetails(a.ctx, a.CustomerUUID, uUUID)
}

// ResetUserPassword resets the password of the user
func (a *AuthAPIClient) ResetUserPassword() ybaclient.UserManagementApiApiResetUserPasswordRequest {
	return a.APIClient.UserManagementApi.ResetUserPassword(a.ctx, a.CustomerUUID)
}

// RetrieveOIDCAuthToken fetches the OIDC JWT auth token
func (a *AuthAPIClient) RetrieveOIDCAuthToken(
	uUUID string,
) ybaclient.UserManagementApiApiRetrieveOIDCAuthTokenRequest {
	return a.APIClient.UserManagementApi.RetrieveOIDCAuthToken(a.ctx, a.CustomerUUID, uUUID)
}

// UpdateUserProfile updates the user profile
func (a *AuthAPIClient) UpdateUserProfile(
	uUUID string,
) ybaclient.UserManagementApiApiUpdateUserProfileRequest {
	return a.APIClient.UserManagementApi.UpdateUserProfile(a.ctx, a.CustomerUUID, uUUID)
}
