/*
 * Copyright (c) YugaByte, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// ListKMSConfigs fetches list of kms configs associated with the customer
func (a *AuthAPIClient) ListKMSConfigs() (
	ybaclient.EncryptionAtRestApiApiListKMSConfigsRequest) {
	return a.APIClient.EncryptionAtRestApi.ListKMSConfigs(a.ctx, a.CustomerUUID)
}

// DeleteKMSConfig deletes kms config
func (a *AuthAPIClient) DeleteKMSConfig(configUUID string) (
	ybaclient.EncryptionAtRestApiApiDeleteKMSConfigRequest) {
	return a.APIClient.EncryptionAtRestApi.DeleteKMSConfig(a.ctx, a.CustomerUUID, configUUID)
}

// CreateKMSConfig creates kms config
func (a *AuthAPIClient) CreateKMSConfig(providerType string) (
	ybaclient.EncryptionAtRestApiApiCreateKMSConfigRequest) {
	return a.APIClient.EncryptionAtRestApi.CreateKMSConfig(a.ctx, a.CustomerUUID, providerType)
}
