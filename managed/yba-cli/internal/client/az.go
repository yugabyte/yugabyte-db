/*
 * Copyright (c) YugaByte, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// ListOfAZ fetches az list of a provider region
func (a *AuthAPIClient) ListOfAZ(pUUID, rUUID string) (
	ybaclient.AvailabilityZonesApiApiListOfAZRequest,
) {
	return a.APIClient.AvailabilityZonesApi.ListOfAZ(a.ctx, a.CustomerUUID, pUUID, rUUID)
}
