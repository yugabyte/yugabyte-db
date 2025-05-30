/*
 * Copyright (c) YugaByte, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// ListOfAlerts fetches list of alerts associated with the customer
func (a *AuthAPIClient) ListOfAlerts() ybaclient.AlertsApiApiListOfAlertsRequest {
	return a.APIClient.AlertsApi.ListOfAlerts(a.ctx, a.CustomerUUID)
}

// PageAlerts fetches list of alerts associated with the customer
func (a *AuthAPIClient) PageAlerts() ybaclient.AlertsApiApiPageAlertsRequest {
	return a.APIClient.AlertsApi.PageAlerts(a.ctx, a.CustomerUUID)
}

// Acknowledge acknowledges the alert
func (a *AuthAPIClient) Acknowledge(alertUUID string) ybaclient.AlertsApiApiAcknowledgeRequest {
	return a.APIClient.AlertsApi.Acknowledge(a.ctx, a.CustomerUUID, alertUUID)
}

// CountAlerts fetches count of alerts associated with the customer
func (a *AuthAPIClient) CountAlerts() ybaclient.AlertsApiApiCountAlertsRequest {
	return a.APIClient.AlertsApi.CountAlerts(a.ctx, a.CustomerUUID)
}

// GetAlert fetches alert associated with the customer and alertUUID
func (a *AuthAPIClient) GetAlert(alertUUID string) ybaclient.AlertsApiApiGetRequest {
	return a.APIClient.AlertsApi.Get(a.ctx, a.CustomerUUID, alertUUID)
}

// ListAlertTemplates fetches list of alert template variables
func (a *AuthAPIClient) ListAlertTemplates() ybaclient.AlertsApiApiListAlertTemplatesRequest {
	return a.APIClient.AlertsApi.ListAlertTemplates(a.ctx, a.CustomerUUID)
}

// AlertNotificationPreview fetches alert notification preview
func (a *AuthAPIClient) AlertNotificationPreview() ybaclient.AlertsApiApiAlertNotificationPreviewRequest {
	return a.APIClient.AlertsApi.AlertNotificationPreview(a.ctx, a.CustomerUUID)
}
