/*
 * Copyright (c) YugaByte, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// ListKeys fetches list of mutable runtime configuration keys
func (a *AuthAPIClient) ListKeys() ybaclient.RuntimeConfigurationApiApiListKeysRequest {
	return a.APIClient.RuntimeConfigurationApi.ListKeys(a.ctx)
}

// ListKeyInfo fetches list of mutable runtime configuration keys
func (a *AuthAPIClient) ListKeyInfo() ybaclient.RuntimeConfigurationApiApiListKeyInfoRequest {
	return a.APIClient.RuntimeConfigurationApi.ListKeyInfo(a.ctx)
}

// ListScopes fetches list of mutable runtime configuration scopes
func (a *AuthAPIClient) ListScopes() ybaclient.RuntimeConfigurationApiApiListScopesRequest {
	return a.APIClient.RuntimeConfigurationApi.ListScopes(a.ctx, a.CustomerUUID)
}

// GetConfig fetches runtime configuration
func (a *AuthAPIClient) GetConfig(
	scope string,
) ybaclient.RuntimeConfigurationApiApiGetConfigRequest {
	return a.APIClient.RuntimeConfigurationApi.GetConfig(a.ctx, a.CustomerUUID, scope)
}

// GetConfigurationKey fetches runtime configuration key
func (a *AuthAPIClient) GetConfigurationKey(
	scope, key string,
) ybaclient.RuntimeConfigurationApiApiGetConfigurationKeyRequest {
	return a.APIClient.RuntimeConfigurationApi.GetConfigurationKey(
		a.ctx,
		a.CustomerUUID,
		scope, key)
}

// SetKey sets runtime configuration key
func (a *AuthAPIClient) SetKey(
	scope, key string,
) ybaclient.RuntimeConfigurationApiApiSetKeyRequest {
	return a.APIClient.RuntimeConfigurationApi.SetKey(
		a.ctx,
		a.CustomerUUID,
		scope, key)
}

// DeleteKey deletes runtime configuration key
func (a *AuthAPIClient) DeleteKey(
	scope, key string,
) ybaclient.RuntimeConfigurationApiApiDeleteKeyRequest {
	return a.APIClient.RuntimeConfigurationApi.DeleteKey(
		a.ctx,
		a.CustomerUUID,
		scope, key)
}

// ListFeatureFlags fetches list of feature flags
func (a *AuthAPIClient) ListFeatureFlags() ybaclient.RuntimeConfigurationApiApiListFeatureFlagsRequest {
	return a.APIClient.RuntimeConfigurationApi.ListFeatureFlags(a.ctx)
}
