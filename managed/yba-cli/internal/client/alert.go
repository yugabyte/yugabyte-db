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

// GetAlertConfiguration fetches alert configuration associated with the customer
func (a *AuthAPIClient) GetAlertConfiguration(
	alertConfigUUID string,
) ybaclient.AlertsApiApiGetAlertConfigurationRequest {
	return a.APIClient.AlertsApi.GetAlertConfiguration(a.ctx, a.CustomerUUID, alertConfigUUID)
}

// ListAlertConfigurations fetches list of alert configurations associated with the customer
func (a *AuthAPIClient) ListAlertConfigurations() ybaclient.AlertsApiApiListAlertConfigurationsRequest {
	return a.APIClient.AlertsApi.ListAlertConfigurations(a.ctx, a.CustomerUUID)
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

// AlertNotificationPreview fetches alert notification preview
func (a *AuthAPIClient) AlertNotificationPreview() ybaclient.AlertsApiApiAlertNotificationPreviewRequest {
	return a.APIClient.AlertsApi.AlertNotificationPreview(a.ctx, a.CustomerUUID)
}

// ListAlertChannels fetches list of alert channels
func (a *AuthAPIClient) ListAlertChannels() ybaclient.AlertsApiApiListAlertChannelsRequest {
	return a.APIClient.AlertsApi.ListAlertChannels(a.ctx, a.CustomerUUID)
}

// GetAlertChannel fetches alert channel
func (a *AuthAPIClient) GetAlertChannel(
	channelUUID string,
) ybaclient.AlertsApiApiGetAlertChannelRequest {
	return a.APIClient.AlertsApi.GetAlertChannel(a.ctx, a.CustomerUUID, channelUUID)
}

// CreateAlertChannel creates alert channel
func (a *AuthAPIClient) CreateAlertChannel() ybaclient.AlertsApiApiCreateAlertChannelRequest {
	return a.APIClient.AlertsApi.CreateAlertChannel(a.ctx, a.CustomerUUID)
}

// UpdateAlertChannel updates alert channel
func (a *AuthAPIClient) UpdateAlertChannel(
	channelUUID string,
) ybaclient.AlertsApiApiUpdateAlertChannelRequest {
	return a.APIClient.AlertsApi.UpdateAlertChannel(a.ctx, a.CustomerUUID, channelUUID)
}

// DeleteAlertChannel deletes alert channel
func (a *AuthAPIClient) DeleteAlertChannel(
	channelUUID string,
) ybaclient.AlertsApiApiDeleteAlertChannelRequest {
	return a.APIClient.AlertsApi.DeleteAlertChannel(a.ctx, a.CustomerUUID, channelUUID)
}

// ListAlertChannelTemplates fetches list of alert channel templates
func (a *AuthAPIClient) ListAlertChannelTemplates() ybaclient.AlertsApiApiListAlertChannelTemplatesRequest {
	return a.APIClient.AlertsApi.ListAlertChannelTemplates(a.ctx, a.CustomerUUID)
}

// GetAlertChannelTemplates fetches alert channel template
func (a *AuthAPIClient) GetAlertChannelTemplates(
	templateUUID string,
) ybaclient.AlertsApiApiGetAlertChannelTemplatesRequest {
	return a.APIClient.AlertsApi.GetAlertChannelTemplates(a.ctx, a.CustomerUUID, templateUUID)
}

// SetAlertChannelTemplates sets alert channel template
func (a *AuthAPIClient) SetAlertChannelTemplates(
	templateUUID string,
) ybaclient.AlertsApiApiSetAlertChannelTemplatesRequest {
	return a.APIClient.AlertsApi.SetAlertChannelTemplates(a.ctx, a.CustomerUUID, templateUUID)
}

// DeleteAlertChannelTemplates deletes alert channel template
func (a *AuthAPIClient) DeleteAlertChannelTemplates(
	templateUUID string,
) ybaclient.AlertsApiApiDeleteAlertChannelTemplatesRequest {
	return a.APIClient.AlertsApi.DeleteAlertChannelTemplates(a.ctx, a.CustomerUUID, templateUUID)
}

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
