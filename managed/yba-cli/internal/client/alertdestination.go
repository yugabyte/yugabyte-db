/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// ListAlertDestinations fetches list of alert destinations
func (a *AuthAPIClient) ListAlertDestinations() ybaclient.AlertsApiApiListAlertDestinationsRequest {
	return a.APIClient.AlertsApi.ListAlertDestinations(a.ctx, a.CustomerUUID)
}

// GetAlertDestination fetches alert destination
func (a *AuthAPIClient) GetAlertDestination(
	destinationUUID string,
) ybaclient.AlertsApiApiGetAlertDestinationRequest {
	return a.APIClient.AlertsApi.GetAlertDestination(a.ctx, a.CustomerUUID, destinationUUID)
}

// CreateAlertDestination creates alert destination
func (a *AuthAPIClient) CreateAlertDestination() ybaclient.AlertsApiApiCreateAlertDestinationRequest {
	return a.APIClient.AlertsApi.CreateAlertDestination(a.ctx, a.CustomerUUID)
}

// UpdateAlertDestination updates alert destination
func (a *AuthAPIClient) UpdateAlertDestination(
	destinationUUID string,
) ybaclient.AlertsApiApiUpdateAlertDestinationRequest {
	return a.APIClient.AlertsApi.UpdateAlertDestination(a.ctx, a.CustomerUUID, destinationUUID)
}

// DeleteAlertDestination deletes alert destination
func (a *AuthAPIClient) DeleteAlertDestination(
	destinationUUID string,
) ybaclient.AlertsApiApiDeleteAlertDestinationRequest {
	return a.APIClient.AlertsApi.DeleteAlertDestination(a.ctx, a.CustomerUUID, destinationUUID)
}
