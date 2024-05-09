/*
 * Copyright (c) YugaByte, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// GetListOfProviders fetches list of providers associated with the customer
func (a *AuthAPIClient) GetListOfProviders() (
	ybaclient.CloudProvidersApiApiGetListOfProvidersRequest) {
	return a.APIClient.CloudProvidersApi.GetListOfProviders(a.ctx, a.CustomerUUID)
}

// GetProvider fetches provider associated with the customer and providerUUID
func (a *AuthAPIClient) GetProvider(pUUID string) (
	ybaclient.CloudProvidersApiApiGetProviderRequest) {
	return a.APIClient.CloudProvidersApi.GetProvider(a.ctx, a.CustomerUUID, pUUID)
}

// CreateProvider calls the create provider API
func (a *AuthAPIClient) CreateProvider() (
	ybaclient.CloudProvidersApiApiCreateProvidersRequest) {
	return a.APIClient.CloudProvidersApi.CreateProviders(a.ctx, a.CustomerUUID)
}

// DeleteProvider deletes provider associated with the providerUUID
func (a *AuthAPIClient) DeleteProvider(pUUID string) (
	ybaclient.CloudProvidersApiApiDeleteRequest) {
	return a.APIClient.CloudProvidersApi.Delete(a.ctx, a.CustomerUUID, pUUID)
}

// EditProvider edits the provider associated with the providerUUIS
func (a *AuthAPIClient) EditProvider(pUUID string) (
	ybaclient.CloudProvidersApiApiEditProviderRequest,
) {
	return a.APIClient.CloudProvidersApi.EditProvider(a.ctx, a.CustomerUUID, pUUID)
}

// List fetches the list of access keys associated with the provider
func (a *AuthAPIClient) List(pUUID string) (
	ybaclient.AccessKeysApiApiListRequest,
) {
	return a.APIClient.AccessKeysApi.List(a.ctx, a.CustomerUUID, pUUID)
}

// NewProviderYBAVersionCheck checks if the new API request body can be used for the Create
// Provider API
func (a *AuthAPIClient) NewProviderYBAVersionCheck() (bool, string, error) {
	allowedVersions := []string{util.YBAAllowNewProviderMinVersion}
	allowed, version, err := a.CheckValidYBAVersion(allowedVersions)
	if err != nil {
		return false, "", err
	}
	return allowed, version, err
}
