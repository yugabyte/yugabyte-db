/*
 * Copyright (c) YugaByte, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// Refresh the releases page to get the latest YugabyteDB releases
func (a *AuthAPIClient) Refresh() (
	ybaclient.ReleaseManagementApiApiRefreshRequest,
) {
	return a.APIClient.ReleaseManagementApi.Refresh(a.ctx, a.CustomerUUID)
}

// GetListOfReleases API to fetch list of releases
func (a *AuthAPIClient) GetListOfReleases(includeMetadata bool) (
	ybaclient.ReleaseManagementApiApiGetListOfReleasesRequest,
) {
	return a.APIClient.ReleaseManagementApi.
		GetListOfReleases(a.ctx, a.CustomerUUID).
		IncludeMetadata(includeMetadata)
}
