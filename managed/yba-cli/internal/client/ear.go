/*
 * Copyright (c) YugaByte, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// ListKMSConfigs fetches list of universes associated with the customer
func (a *AuthAPIClient) ListKMSConfigs() (
	ybaclient.EncryptionAtRestApiApiListKMSConfigsRequest) {
	return a.APIClient.EncryptionAtRestApi.ListKMSConfigs(a.ctx, a.CustomerUUID)
}
