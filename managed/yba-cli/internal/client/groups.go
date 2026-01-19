/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import ybav2client "github.com/yugabyte/platform-go-client/v2"

// DeleteGroupMappings deletes group mappings
func (a *AuthAPIClient) DeleteGroupMappings(
	groupUUID string,
) ybav2client.AuthenticationAPIDeleteGroupMappingsRequest {
	return a.APIv2Client.AuthenticationAPI.DeleteGroupMappings(a.ctx, a.CustomerUUID, groupUUID)
}

// ListMappings lists group mappings
func (a *AuthAPIClient) ListMappings() ybav2client.AuthenticationAPIListMappingsRequest {
	return a.APIv2Client.AuthenticationAPI.ListMappings(a.ctx, a.CustomerUUID)
}

// UpdateGroupMappings updates group mappings
func (a *AuthAPIClient) UpdateGroupMappings() ybav2client.AuthenticationAPIUpdateGroupMappingsRequest {
	return a.APIv2Client.AuthenticationAPI.UpdateGroupMappings(a.ctx, a.CustomerUUID)
}
