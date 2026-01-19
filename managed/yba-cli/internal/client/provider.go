/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// GetListOfProviders fetches list of providers associated with the customer
func (a *AuthAPIClient) GetListOfProviders() ybaclient.CloudProvidersAPIGetListOfProvidersRequest {
	return a.APIClient.CloudProvidersAPI.GetListOfProviders(a.ctx, a.CustomerUUID)
}

// GetProvider fetches provider associated with the customer and providerUUID
func (a *AuthAPIClient) GetProvider(pUUID string) ybaclient.CloudProvidersAPIGetProviderRequest {
	return a.APIClient.CloudProvidersAPI.GetProvider(a.ctx, a.CustomerUUID, pUUID)
}

// CreateProvider calls the create provider API
func (a *AuthAPIClient) CreateProvider() ybaclient.CloudProvidersAPICreateProvidersRequest {
	return a.APIClient.CloudProvidersAPI.CreateProviders(a.ctx, a.CustomerUUID)
}

// DeleteProvider deletes provider associated with the providerUUID
func (a *AuthAPIClient) DeleteProvider(pUUID string) ybaclient.CloudProvidersAPIDeleteRequest {
	return a.APIClient.CloudProvidersAPI.Delete(a.ctx, a.CustomerUUID, pUUID)
}

// EditProvider edits the provider associated with the providerUUIS
func (a *AuthAPIClient) EditProvider(
	pUUID string,
) ybaclient.CloudProvidersAPIEditProviderRequest {
	return a.APIClient.CloudProvidersAPI.EditProvider(a.ctx, a.CustomerUUID, pUUID)
}

// List fetches the list of access keys associated with the provider
func (a *AuthAPIClient) List(pUUID string) ybaclient.AccessKeysAPIListRequest {
	return a.APIClient.AccessKeysAPI.List(a.ctx, a.CustomerUUID, pUUID)
}

// NewProviderYBAVersionCheck checks if the new API request body can be used for the Create
// Provider API
func (a *AuthAPIClient) NewProviderYBAVersionCheck() (bool, string, error) {
	allowedVersions := YBAMinimumVersion{
		Stable:  util.YBAAllowNewProviderMinVersion,
		Preview: util.YBAAllowNewProviderMinVersion,
	}
	allowed, version, err := a.CheckValidYBAVersion(allowedVersions)
	if err != nil {
		return false, "", err
	}
	return allowed, version, err
}
