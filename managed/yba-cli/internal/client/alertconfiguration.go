/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// ListAlertConfigurations fetches list of alert configurations associated with the customer
func (a *AuthAPIClient) ListAlertConfigurations() ybaclient.AlertsAPIListAlertConfigurationsRequest {
	return a.APIClient.AlertsAPI.ListAlertConfigurations(a.ctx, a.CustomerUUID)
}

// GetAlertConfiguration fetches alert configuration associated with the customer
func (a *AuthAPIClient) GetAlertConfiguration(
	alertConfigUUID string,
) ybaclient.AlertsAPIGetAlertConfigurationRequest {
	return a.APIClient.AlertsAPI.GetAlertConfiguration(a.ctx, a.CustomerUUID, alertConfigUUID)
}

// PageAlertConfigurations fetches list of alert configurations associated with the customer
func (a *AuthAPIClient) PageAlertConfigurations() ybaclient.AlertsAPIPageAlertConfigurationsRequest {
	return a.APIClient.AlertsAPI.PageAlertConfigurations(a.ctx, a.CustomerUUID)
}

// CreateAlertConfiguration creates alert configuration
func (a *AuthAPIClient) CreateAlertConfiguration() ybaclient.AlertsAPICreateAlertConfigurationRequest {
	return a.APIClient.AlertsAPI.CreateAlertConfiguration(a.ctx, a.CustomerUUID)
}

// UpdateAlertConfiguration updates alert configuration
func (a *AuthAPIClient) UpdateAlertConfiguration(
	alertConfigUUID string,
) ybaclient.AlertsAPIUpdateAlertConfigurationRequest {
	return a.APIClient.AlertsAPI.UpdateAlertConfiguration(a.ctx, a.CustomerUUID, alertConfigUUID)
}

// DeleteAlertConfiguration deletes alert configuration
func (a *AuthAPIClient) DeleteAlertConfiguration(
	alertConfigUUID string,
) ybaclient.AlertsAPIDeleteAlertConfigurationRequest {
	return a.APIClient.AlertsAPI.DeleteAlertConfiguration(a.ctx, a.CustomerUUID, alertConfigUUID)
}

// SendTestAlert tests alert
func (a *AuthAPIClient) SendTestAlert(
	alertConfigUUID string,
) ybaclient.AlertsAPISendTestAlertRequest {
	return a.APIClient.AlertsAPI.SendTestAlert(a.ctx, a.CustomerUUID, alertConfigUUID)
}
