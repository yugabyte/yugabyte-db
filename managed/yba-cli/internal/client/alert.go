/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// ListOfAlerts fetches list of alerts associated with the customer
func (a *AuthAPIClient) ListOfAlerts() ybaclient.AlertsAPIListOfAlertsRequest {
	return a.APIClient.AlertsAPI.ListOfAlerts(a.ctx, a.CustomerUUID)
}

// PageAlerts fetches list of alerts associated with the customer
func (a *AuthAPIClient) PageAlerts() ybaclient.AlertsAPIPageAlertsRequest {
	return a.APIClient.AlertsAPI.PageAlerts(a.ctx, a.CustomerUUID)
}

// Acknowledge acknowledges the alert
func (a *AuthAPIClient) Acknowledge(alertUUID string) ybaclient.AlertsAPIAcknowledgeRequest {
	return a.APIClient.AlertsAPI.Acknowledge(a.ctx, a.CustomerUUID, alertUUID)
}

// CountAlerts fetches count of alerts associated with the customer
func (a *AuthAPIClient) CountAlerts() ybaclient.AlertsAPICountAlertsRequest {
	return a.APIClient.AlertsAPI.CountAlerts(a.ctx, a.CustomerUUID)
}

// GetAlert fetches alert associated with the customer and alertUUID
func (a *AuthAPIClient) GetAlert(alertUUID string) ybaclient.AlertsAPIGetRequest {
	return a.APIClient.AlertsAPI.Get(a.ctx, a.CustomerUUID, alertUUID)
}

// ListAlertTemplates fetches list of alert template variables
func (a *AuthAPIClient) ListAlertTemplates() ybaclient.AlertsAPIListAlertTemplatesRequest {
	return a.APIClient.AlertsAPI.ListAlertTemplates(a.ctx, a.CustomerUUID)
}

// AlertNotificationPreview fetches alert notification preview
func (a *AuthAPIClient) AlertNotificationPreview() ybaclient.AlertsAPIAlertNotificationPreviewRequest {
	return a.APIClient.AlertsAPI.AlertNotificationPreview(a.ctx, a.CustomerUUID)
}
