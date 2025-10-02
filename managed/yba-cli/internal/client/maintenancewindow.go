/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// ListOfMaintenanceWindows fetches list of maintenance windows associated with the customer
func (a *AuthAPIClient) ListOfMaintenanceWindows() ybaclient.MaintenanceWindowsApiApiListOfMaintenanceWindowsRequest {
	return a.APIClient.MaintenanceWindowsApi.ListOfMaintenanceWindows(a.ctx, a.CustomerUUID)
}

// Page fetches list of maintenance windows associated with the customer
func (a *AuthAPIClient) Page() ybaclient.MaintenanceWindowsApiApiPageRequest {
	return a.APIClient.MaintenanceWindowsApi.Page(a.ctx, a.CustomerUUID)
}

// GetMaintenanceWindow fetches maintenance window associated with
// the customer and maintenanceWindowUUID
func (a *AuthAPIClient) GetMaintenanceWindow(
	maintenanceWindowUUID string,
) ybaclient.MaintenanceWindowsApiApiGetRequest {
	return a.APIClient.MaintenanceWindowsApi.Get(a.ctx, a.CustomerUUID, maintenanceWindowUUID)
}

// CreateMaintenanceWindow calls the create maintenance window API
func (a *AuthAPIClient) CreateMaintenanceWindow() ybaclient.MaintenanceWindowsApiApiCreateRequest {
	return a.APIClient.MaintenanceWindowsApi.Create(a.ctx, a.CustomerUUID)
}

// DeleteMaintenanceWindow deletes maintenance window associated with the maintenanceWindowUUID
func (a *AuthAPIClient) DeleteMaintenanceWindow(
	maintenanceWindowUUID string,
) ybaclient.MaintenanceWindowsApiApiDeleteRequest {
	return a.APIClient.MaintenanceWindowsApi.Delete(a.ctx, a.CustomerUUID, maintenanceWindowUUID)
}

// UpdateMaintenanceWindow updates the maintenance window associated with the maintenanceWindowUUID
func (a *AuthAPIClient) UpdateMaintenanceWindow(
	maintenanceWindowUUID string,
) ybaclient.MaintenanceWindowsApiApiUpdateRequest {
	return a.APIClient.MaintenanceWindowsApi.Update(a.ctx, a.CustomerUUID, maintenanceWindowUUID)
}
