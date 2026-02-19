/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	"encoding/json"
	"fmt"
	"net/http"

	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// ListAlertChannels fetches list of alert channels
func (a *AuthAPIClient) ListAlertChannels() ybaclient.AlertsAPIListAlertChannelsRequest {
	return a.APIClient.AlertsAPI.ListAlertChannels(a.ctx, a.CustomerUUID)
}

// GetAlertChannel fetches alert channel
func (a *AuthAPIClient) GetAlertChannel(
	channelUUID string,
) ybaclient.AlertsAPIGetAlertChannelRequest {
	return a.APIClient.AlertsAPI.GetAlertChannel(a.ctx, a.CustomerUUID, channelUUID)
}

// CreateAlertChannel creates alert channel
func (a *AuthAPIClient) CreateAlertChannel() ybaclient.AlertsAPICreateAlertChannelRequest {
	return a.APIClient.AlertsAPI.CreateAlertChannel(a.ctx, a.CustomerUUID)
}

// UpdateAlertChannel updates alert channel
func (a *AuthAPIClient) UpdateAlertChannel(
	channelUUID string,
) ybaclient.AlertsAPIUpdateAlertChannelRequest {
	return a.APIClient.AlertsAPI.UpdateAlertChannel(a.ctx, a.CustomerUUID, channelUUID)
}

// DeleteAlertChannel deletes alert channel
func (a *AuthAPIClient) DeleteAlertChannel(
	channelUUID string,
) ybaclient.AlertsAPIDeleteAlertChannelRequest {
	return a.APIClient.AlertsAPI.DeleteAlertChannel(a.ctx, a.CustomerUUID, channelUUID)
}

// ListAlertChannelTemplates fetches list of alert channel templates
func (a *AuthAPIClient) ListAlertChannelTemplates() ybaclient.AlertsAPIListAlertChannelTemplatesRequest {
	return a.APIClient.AlertsAPI.ListAlertChannelTemplates(a.ctx, a.CustomerUUID)
}

// GetAlertChannelTemplates fetches alert channel template
func (a *AuthAPIClient) GetAlertChannelTemplates(
	templateUUID string,
) ybaclient.AlertsAPIGetAlertChannelTemplatesRequest {
	return a.APIClient.AlertsAPI.GetAlertChannelTemplates(a.ctx, a.CustomerUUID, templateUUID)
}

// SetAlertChannelTemplates sets alert channel template
func (a *AuthAPIClient) SetAlertChannelTemplates(
	templateUUID string,
) ybaclient.AlertsAPISetAlertChannelTemplatesRequest {
	return a.APIClient.AlertsAPI.SetAlertChannelTemplates(a.ctx, a.CustomerUUID, templateUUID)
}

// DeleteAlertChannelTemplates deletes alert channel template
func (a *AuthAPIClient) DeleteAlertChannelTemplates(
	templateUUID string,
) ybaclient.AlertsAPIDeleteAlertChannelTemplatesRequest {
	return a.APIClient.AlertsAPI.DeleteAlertChannelTemplates(a.ctx, a.CustomerUUID, templateUUID)
}

// ListAlertChannelsRest uses REST API to call list schedule functionality
func (a *AuthAPIClient) ListAlertChannelsRest(
	callSite,
	operation string,
) (
	[]util.AlertChannel, error,
) {
	errorTag := fmt.Errorf("%s, Operation: %s", callSite, operation)

	body, err := a.RestAPICall(
		RestAPIParameters{
			reqBytes:        nil,
			urlRoute:        "alert_channels",
			method:          http.MethodGet,
			operationString: "list alert channels",
		},
	)
	if err != nil {
		return nil,
			fmt.Errorf("%w: %s", errorTag, err.Error())
	}

	responseBody := []util.AlertChannel{}
	if err = json.Unmarshal(body, &responseBody); err != nil {
		return nil,
			fmt.Errorf("%w: Failed unmarshalling list alert channel response body %s",
				errorTag,
				err.Error())
	}

	if responseBody != nil {
		return responseBody, nil
	}

	responseBodyError := util.YbaStructuredError{}
	if err = json.Unmarshal(body, &responseBodyError); err != nil {
		return nil,
			fmt.Errorf("%w: Failed unmarshalling list alert channel error response body %s",
				errorTag,
				err.Error())
	}

	errorMessage := util.ErrorFromResponseBody(responseBodyError)
	return nil,
		fmt.Errorf("%w: Error fetching list of alert channels: %s", errorTag, errorMessage)

}

// CreateAlertChannelRest uses REST API to call list schedule functionality
func (a *AuthAPIClient) CreateAlertChannelRest(
	reqBody util.AlertChannelFormData,
	callSite string,
) (
	util.AlertChannel, error,
) {
	errorTag := fmt.Errorf("%s, Operation: Create", callSite)

	reqBytes, err := json.Marshal(reqBody)
	if err != nil {
		return util.AlertChannel{},
			fmt.Errorf("%w: %s", errorTag, err.Error())
	}

	body, err := a.RestAPICall(
		RestAPIParameters{
			reqBytes:        reqBytes,
			urlRoute:        "alert_channels",
			method:          http.MethodPost,
			operationString: "create alert channel",
		},
	)
	if err != nil {
		return util.AlertChannel{},
			fmt.Errorf("%w: %s", errorTag, err.Error())
	}

	responseBody := util.AlertChannel{}
	if err = json.Unmarshal(body, &responseBody); err != nil {
		return util.AlertChannel{},
			fmt.Errorf("%w: Failed unmarshalling create alert channel response body %s",
				errorTag,
				err.Error())
	}

	if responseBody.Uuid != nil {
		return responseBody, nil
	}

	responseBodyError := util.YbaStructuredError{}
	if err = json.Unmarshal(body, &responseBodyError); err != nil {
		return util.AlertChannel{},
			fmt.Errorf("%w: Failed unmarshalling create alert channel error response body %s",
				errorTag,
				err.Error())
	}

	errorMessage := util.ErrorFromResponseBody(responseBodyError)
	return util.AlertChannel{},
		fmt.Errorf("%w: Error fetching create of alert channels: %s", errorTag, errorMessage)

}

// UpdateAlertChannelRest uses REST API to call list schedule functionality
func (a *AuthAPIClient) UpdateAlertChannelRest(
	uuid string,
	callSite string,
	reqBody util.AlertChannelFormData,
) (
	util.AlertChannel, error,
) {
	errorTag := fmt.Errorf("%s, Operation: Update", callSite)

	reqBytes, err := json.Marshal(reqBody)
	if err != nil {
		return util.AlertChannel{},
			fmt.Errorf("%w: %s", errorTag, err.Error())
	}

	body, err := a.RestAPICall(
		RestAPIParameters{
			reqBytes:        reqBytes,
			urlRoute:        fmt.Sprintf("alert_channels/%s", uuid),
			method:          http.MethodPut,
			operationString: "update alert channel",
		},
	)
	if err != nil {
		return util.AlertChannel{},
			fmt.Errorf("%w: %s", errorTag, err.Error())
	}

	responseBody := util.AlertChannel{}
	if err = json.Unmarshal(body, &responseBody); err != nil {
		return util.AlertChannel{},
			fmt.Errorf("%w: Failed unmarshalling update alert channel response body %s",
				errorTag,
				err.Error())
	}

	if responseBody.Uuid != nil {
		return responseBody, nil
	}

	responseBodyError := util.YbaStructuredError{}
	if err = json.Unmarshal(body, &responseBodyError); err != nil {
		return util.AlertChannel{},
			fmt.Errorf("%w: Failed unmarshalling update alert channel error response body %s",
				errorTag,
				err.Error())
	}

	errorMessage := util.ErrorFromResponseBody(responseBodyError)
	return util.AlertChannel{},
		fmt.Errorf("%w: Error fetching update of alert channels: %s", errorTag, errorMessage)

}
