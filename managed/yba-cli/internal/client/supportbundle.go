/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import ybaclient "github.com/yugabyte/platform-go-client"

type listSupportBundleRequest = ybaclient.SupportBundleManagementAPIListSupportBundleComponentsRequest

// ListSupportBundleComponents fetches list of support bundle components
func (a *AuthAPIClient) ListSupportBundleComponents() listSupportBundleRequest {
	return a.APIClient.SupportBundleManagementAPI.ListSupportBundleComponents(a.ctx, a.CustomerUUID)
}

// DeleteSupportBundle deletes support bundle
func (a *AuthAPIClient) DeleteSupportBundle(
	universeUUID string,
	supportBundleUUID string,
) ybaclient.SupportBundleManagementAPIDeleteSupportBundleRequest {
	return a.APIClient.SupportBundleManagementAPI.DeleteSupportBundle(
		a.ctx,
		a.CustomerUUID,
		universeUUID,
		supportBundleUUID,
	)
}

// CreateSupportBundle creates support bundle
func (a *AuthAPIClient) CreateSupportBundle(
	universeUUID string,
) ybaclient.SupportBundleManagementAPICreateSupportBundleRequest {
	return a.APIClient.SupportBundleManagementAPI.CreateSupportBundle(
		a.ctx,
		a.CustomerUUID,
		universeUUID,
	)
}

// GetSupportBundle fetches support bundle
func (a *AuthAPIClient) GetSupportBundle(
	universeUUID string,
	supportBundleUUID string,
) ybaclient.SupportBundleManagementAPIGetSupportBundleRequest {
	return a.APIClient.SupportBundleManagementAPI.GetSupportBundle(
		a.ctx,
		a.CustomerUUID,
		universeUUID,
		supportBundleUUID,
	)
}

// ListSupportBundle fetches list of support bundles
func (a *AuthAPIClient) ListSupportBundle(
	universeUUID string,
) ybaclient.SupportBundleManagementAPIListSupportBundleRequest {
	return a.APIClient.SupportBundleManagementAPI.ListSupportBundle(
		a.ctx,
		a.CustomerUUID,
		universeUUID,
	)
}

// DownloadSupportBundle downloads support bundle
func (a *AuthAPIClient) DownloadSupportBundle(
	universeUUID string,
	supportBundleUUID string,
) ybaclient.SupportBundleManagementAPIDownloadSupportBundleRequest {
	return a.APIClient.SupportBundleManagementAPI.DownloadSupportBundle(
		a.ctx,
		a.CustomerUUID,
		universeUUID,
		supportBundleUUID,
	)
}
