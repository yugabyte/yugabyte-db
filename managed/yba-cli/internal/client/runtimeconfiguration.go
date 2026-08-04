/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// ListKeys fetches list of mutable runtime configuration keys
func (a *AuthAPIClient) ListKeys() ybaclient.RuntimeConfigurationAPIListKeysRequest {
	return a.APIClient.RuntimeConfigurationAPI.ListKeys(a.ctx)
}

// ListKeyInfo fetches list of mutable runtime configuration keys
func (a *AuthAPIClient) ListKeyInfo() ybaclient.RuntimeConfigurationAPIListKeyInfoRequest {
	return a.APIClient.RuntimeConfigurationAPI.ListKeyInfo(a.ctx)
}

// ListScopes fetches list of mutable runtime configuration scopes
func (a *AuthAPIClient) ListScopes() ybaclient.RuntimeConfigurationAPIListScopesRequest {
	return a.APIClient.RuntimeConfigurationAPI.ListScopes(a.ctx, a.CustomerUUID)
}

// GetConfig fetches runtime configuration
func (a *AuthAPIClient) GetConfig(
	scope string,
) ybaclient.RuntimeConfigurationAPIGetConfigRequest {
	return a.APIClient.RuntimeConfigurationAPI.GetConfig(a.ctx, a.CustomerUUID, scope)
}

// GetConfigurationKey fetches runtime configuration key
func (a *AuthAPIClient) GetConfigurationKey(
	scope, key string,
) ybaclient.RuntimeConfigurationAPIGetConfigurationKeyRequest {
	return a.APIClient.RuntimeConfigurationAPI.GetConfigurationKey(
		a.ctx,
		a.CustomerUUID,
		scope, key)
}

// SetKey sets runtime configuration key
func (a *AuthAPIClient) SetKey(
	scope, key string,
) ybaclient.RuntimeConfigurationAPISetKeyRequest {
	return a.APIClient.RuntimeConfigurationAPI.SetKey(
		a.ctx,
		a.CustomerUUID,
		scope, key)
}

// DeleteKey deletes runtime configuration key
func (a *AuthAPIClient) DeleteKey(
	scope, key string,
) ybaclient.RuntimeConfigurationAPIDeleteKeyRequest {
	return a.APIClient.RuntimeConfigurationAPI.DeleteKey(
		a.ctx,
		a.CustomerUUID,
		scope, key)
}

// ListFeatureFlags fetches list of feature flags
func (a *AuthAPIClient) ListFeatureFlags() ybaclient.RuntimeConfigurationAPIListFeatureFlagsRequest {
	return a.APIClient.RuntimeConfigurationAPI.ListFeatureFlags(a.ctx)
}
