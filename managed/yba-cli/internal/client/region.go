/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// GetRegion fetches region list of a provider
func (a *AuthAPIClient) GetRegion(pUUID string) ybaclient.RegionManagementAPIGetRegionRequest {
	return a.APIClient.RegionManagementAPI.GetRegion(a.ctx, a.CustomerUUID, pUUID)
}
