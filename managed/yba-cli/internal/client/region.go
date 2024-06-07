/*
 * Copyright (c) YugaByte, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// GetRegion fetches region list of a provider
func (a *AuthAPIClient) GetRegion(pUUID string) (
	ybaclient.RegionManagementApiApiGetRegionRequest,
) {
	return a.APIClient.RegionManagementApi.GetRegion(a.ctx, a.CustomerUUID, pUUID)
}
