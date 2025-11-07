/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// ListAlertConfigurations fetches list of alert configurations associated with the customer
func (a *AuthAPIClient) ListAlertConfigurations() ybaclient.AlertsApiApiListAlertConfigurationsRequest {
	return a.APIClient.AlertsApi.ListAlertConfigurations(a.ctx, a.CustomerUUID)
}

// GetAlertConfiguration fetches alert configuration associated with the customer
func (a *AuthAPIClient) GetAlertConfiguration(
	alertConfigUUID string,
) ybaclient.AlertsApiApiGetAlertConfigurationRequest {
	return a.APIClient.AlertsApi.GetAlertConfiguration(a.ctx, a.CustomerUUID, alertConfigUUID)
}

// PageAlertConfigurations fetches list of alert configurations associated with the customer
func (a *AuthAPIClient) PageAlertConfigurations() ybaclient.AlertsApiApiPageAlertConfigurationsRequest {
	return a.APIClient.AlertsApi.PageAlertConfigurations(a.ctx, a.CustomerUUID)
}

// CreateAlertConfiguration creates alert configuration
func (a *AuthAPIClient) CreateAlertConfiguration() ybaclient.AlertsApiApiCreateAlertConfigurationRequest {
	return a.APIClient.AlertsApi.CreateAlertConfiguration(a.ctx, a.CustomerUUID)
}

// UpdateAlertConfiguration updates alert configuration
func (a *AuthAPIClient) UpdateAlertConfiguration(
	alertConfigUUID string,
) ybaclient.AlertsApiApiUpdateAlertConfigurationRequest {
	return a.APIClient.AlertsApi.UpdateAlertConfiguration(a.ctx, a.CustomerUUID, alertConfigUUID)
}

// DeleteAlertConfiguration deletes alert configuration
func (a *AuthAPIClient) DeleteAlertConfiguration(
	alertConfigUUID string,
) ybaclient.AlertsApiApiDeleteAlertConfigurationRequest {
	return a.APIClient.AlertsApi.DeleteAlertConfiguration(a.ctx, a.CustomerUUID, alertConfigUUID)
}

// SendTestAlert tests alert
func (a *AuthAPIClient) SendTestAlert(
	alertConfigUUID string,
) ybaclient.AlertsApiApiSendTestAlertRequest {
	return a.APIClient.AlertsApi.SendTestAlert(a.ctx, a.CustomerUUID, alertConfigUUID)
}
