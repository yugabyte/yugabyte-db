/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import ybaclient "github.com/yugabyte/platform-go-client"

type listSupportBundleRequest = ybaclient.SupportBundleManagementApiApiListSupportBundleComponentsRequest

// ListSupportBundleComponents fetches list of support bundle components
func (a *AuthAPIClient) ListSupportBundleComponents() listSupportBundleRequest {
	return a.APIClient.SupportBundleManagementApi.ListSupportBundleComponents(a.ctx, a.CustomerUUID)
}

// DeleteSupportBundle deletes support bundle
func (a *AuthAPIClient) DeleteSupportBundle(
	universeUUID string,
	supportBundleUUID string,
) ybaclient.SupportBundleManagementApiApiDeleteSupportBundleRequest {
	return a.APIClient.SupportBundleManagementApi.DeleteSupportBundle(
		a.ctx,
		a.CustomerUUID,
		universeUUID,
		supportBundleUUID,
	)
}

// CreateSupportBundle creates support bundle
func (a *AuthAPIClient) CreateSupportBundle(
	universeUUID string,
) ybaclient.SupportBundleManagementApiApiCreateSupportBundleRequest {
	return a.APIClient.SupportBundleManagementApi.CreateSupportBundle(
		a.ctx,
		a.CustomerUUID,
		universeUUID,
	)
}

// GetSupportBundle fetches support bundle
func (a *AuthAPIClient) GetSupportBundle(
	universeUUID string,
	supportBundleUUID string,
) ybaclient.SupportBundleManagementApiApiGetSupportBundleRequest {
	return a.APIClient.SupportBundleManagementApi.GetSupportBundle(
		a.ctx,
		a.CustomerUUID,
		universeUUID,
		supportBundleUUID,
	)
}

// ListSupportBundle fetches list of support bundles
func (a *AuthAPIClient) ListSupportBundle(
	universeUUID string,
) ybaclient.SupportBundleManagementApiApiListSupportBundleRequest {
	return a.APIClient.SupportBundleManagementApi.ListSupportBundle(
		a.ctx,
		a.CustomerUUID,
		universeUUID,
	)
}

// DownloadSupportBundle downloads support bundle
func (a *AuthAPIClient) DownloadSupportBundle(
	universeUUID string,
	supportBundleUUID string,
) ybaclient.SupportBundleManagementApiApiDownloadSupportBundleRequest {
	return a.APIClient.SupportBundleManagementApi.DownloadSupportBundle(
		a.ctx,
		a.CustomerUUID,
		universeUUID,
		supportBundleUUID,
	)
}
