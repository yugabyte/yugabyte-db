/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// ListOfMaintenanceWindows fetches list of maintenance windows associated with the customer
func (a *AuthAPIClient) ListOfMaintenanceWindows() ybaclient.MaintenanceWindowsAPIListOfMaintenanceWindowsRequest {
	return a.APIClient.MaintenanceWindowsAPI.ListOfMaintenanceWindows(a.ctx, a.CustomerUUID)
}

// Page fetches list of maintenance windows associated with the customer
func (a *AuthAPIClient) Page() ybaclient.MaintenanceWindowsAPIPageRequest {
	return a.APIClient.MaintenanceWindowsAPI.Page(a.ctx, a.CustomerUUID)
}

// GetMaintenanceWindow fetches maintenance window associated with
// the customer and maintenanceWindowUUID
func (a *AuthAPIClient) GetMaintenanceWindow(
	maintenanceWindowUUID string,
) ybaclient.MaintenanceWindowsAPIGetRequest {
	return a.APIClient.MaintenanceWindowsAPI.Get(a.ctx, a.CustomerUUID, maintenanceWindowUUID)
}

// CreateMaintenanceWindow calls the create maintenance window API
func (a *AuthAPIClient) CreateMaintenanceWindow() ybaclient.MaintenanceWindowsAPICreateRequest {
	return a.APIClient.MaintenanceWindowsAPI.Create(a.ctx, a.CustomerUUID)
}

// DeleteMaintenanceWindow deletes maintenance window associated with the maintenanceWindowUUID
func (a *AuthAPIClient) DeleteMaintenanceWindow(
	maintenanceWindowUUID string,
) ybaclient.MaintenanceWindowsAPIDeleteRequest {
	return a.APIClient.MaintenanceWindowsAPI.Delete(a.ctx, a.CustomerUUID, maintenanceWindowUUID)
}

// UpdateMaintenanceWindow updates the maintenance window associated with the maintenanceWindowUUID
func (a *AuthAPIClient) UpdateMaintenanceWindow(
	maintenanceWindowUUID string,
) ybaclient.MaintenanceWindowsAPIUpdateRequest {
	return a.APIClient.MaintenanceWindowsAPI.Update(a.ctx, a.CustomerUUID, maintenanceWindowUUID)
}
