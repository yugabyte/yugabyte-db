/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// ListAlertDestinations fetches list of alert destinations
func (a *AuthAPIClient) ListAlertDestinations() ybaclient.AlertsAPIListAlertDestinationsRequest {
	return a.APIClient.AlertsAPI.ListAlertDestinations(a.ctx, a.CustomerUUID)
}

// GetAlertDestination fetches alert destination
func (a *AuthAPIClient) GetAlertDestination(
	destinationUUID string,
) ybaclient.AlertsAPIGetAlertDestinationRequest {
	return a.APIClient.AlertsAPI.GetAlertDestination(a.ctx, a.CustomerUUID, destinationUUID)
}

// CreateAlertDestination creates alert destination
func (a *AuthAPIClient) CreateAlertDestination() ybaclient.AlertsAPICreateAlertDestinationRequest {
	return a.APIClient.AlertsAPI.CreateAlertDestination(a.ctx, a.CustomerUUID)
}

// UpdateAlertDestination updates alert destination
func (a *AuthAPIClient) UpdateAlertDestination(
	destinationUUID string,
) ybaclient.AlertsAPIUpdateAlertDestinationRequest {
	return a.APIClient.AlertsAPI.UpdateAlertDestination(a.ctx, a.CustomerUUID, destinationUUID)
}

// DeleteAlertDestination deletes alert destination
func (a *AuthAPIClient) DeleteAlertDestination(
	destinationUUID string,
) ybaclient.AlertsAPIDeleteAlertDestinationRequest {
	return a.APIClient.AlertsAPI.DeleteAlertDestination(a.ctx, a.CustomerUUID, destinationUUID)
}
