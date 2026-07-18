/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// ListUsers fetches list of users associated with the customer
func (a *AuthAPIClient) ListUsers() ybaclient.UserManagementAPIListUsersRequest {
	return a.APIClient.UserManagementAPI.ListUsers(a.ctx, a.CustomerUUID)
}

// CreateUser creates a new user
func (a *AuthAPIClient) CreateUser() ybaclient.UserManagementAPICreateUserRequest {
	return a.APIClient.UserManagementAPI.CreateUser(a.ctx, a.CustomerUUID)
}

// DeleteUser deletes a user
func (a *AuthAPIClient) DeleteUser(uUUID string) ybaclient.UserManagementAPIDeleteUserRequest {
	return a.APIClient.UserManagementAPI.DeleteUser(a.ctx, a.CustomerUUID, uUUID)
}

// GetUserDetails fetches user details
func (a *AuthAPIClient) GetUserDetails(
	uUUID string,
) ybaclient.UserManagementAPIGetUserDetailsRequest {
	return a.APIClient.UserManagementAPI.GetUserDetails(a.ctx, a.CustomerUUID, uUUID)
}

// ResetUserPassword resets the password of the user
func (a *AuthAPIClient) ResetUserPassword() ybaclient.UserManagementAPIResetUserPasswordRequest {
	return a.APIClient.UserManagementAPI.ResetUserPassword(a.ctx, a.CustomerUUID)
}

// RetrieveOIDCAuthToken fetches the OIDC JWT auth token
func (a *AuthAPIClient) RetrieveOIDCAuthToken(
	uUUID string,
) ybaclient.UserManagementAPIRetrieveOIDCAuthTokenRequest {
	return a.APIClient.UserManagementAPI.RetrieveOIDCAuthToken(a.ctx, a.CustomerUUID, uUUID)
}

// UpdateUserProfile updates the user profile
func (a *AuthAPIClient) UpdateUserProfile(
	uUUID string,
) ybaclient.UserManagementAPIUpdateUserProfileRequest {
	return a.APIClient.UserManagementAPI.UpdateUserProfile(a.ctx, a.CustomerUUID, uUUID)
}
