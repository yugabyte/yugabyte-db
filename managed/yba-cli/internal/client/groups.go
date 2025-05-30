/*
 * Copyright (c) YugaByte, Inc.
 */

package client

import ybav2client "github.com/yugabyte/platform-go-client/v2"

// DeleteGroupMappings deletes group mappings
func (a *AuthAPIClient) DeleteGroupMappings(
	groupUUID string,
) ybav2client.AuthenticationApiApiDeleteGroupMappingsRequest {
	return a.APIv2Client.AuthenticationApi.DeleteGroupMappings(a.ctx, a.CustomerUUID, groupUUID)
}

// ListMappings lists group mappings
func (a *AuthAPIClient) ListMappings() ybav2client.AuthenticationApiApiListMappingsRequest {
	return a.APIv2Client.AuthenticationApi.ListMappings(a.ctx, a.CustomerUUID)
}

// UpdateGroupMappings updates group mappings
func (a *AuthAPIClient) UpdateGroupMappings() ybav2client.AuthenticationApiApiUpdateGroupMappingsRequest {
	return a.APIv2Client.AuthenticationApi.UpdateGroupMappings(a.ctx, a.CustomerUUID)
}
