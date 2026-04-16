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

// ListAllTelemetryProviders fetches list of telemetry providers associated with the customer
func (a *AuthAPIClient) ListAllTelemetryProviders() ybaclient.TelemetryProviderAPIListAllTelemetryProvidersRequest {
	return a.APIClient.TelemetryProviderAPI.ListAllTelemetryProviders(a.ctx, a.CustomerUUID)
}

// GetTelemetryProvider fetches telemetry provider associated with the customer and telemetryProviderUUID
func (a *AuthAPIClient) GetTelemetryProvider(
	tpUUID string,
) ybaclient.TelemetryProviderAPIGetTelemetryProviderRequest {
	return a.APIClient.TelemetryProviderAPI.GetTelemetryProvider(a.ctx, a.CustomerUUID, tpUUID)
}

// CreateTelemetry calls the create telemetry provider API
func (a *AuthAPIClient) CreateTelemetry() ybaclient.TelemetryProviderAPICreateTelemetryRequest {
	return a.APIClient.TelemetryProviderAPI.CreateTelemetry(a.ctx, a.CustomerUUID)
}

// DeleteTelemetryProvider deletes telemetry provider associated with the telemetryProviderUUID
func (a *AuthAPIClient) DeleteTelemetryProvider(
	tpUUID string,
) ybaclient.TelemetryProviderAPIDeleteTelemetryProviderRequest {
	return a.APIClient.TelemetryProviderAPI.DeleteTelemetryProvider(a.ctx, a.CustomerUUID, tpUUID)
}

// TelemetryProviderYBAVersionCheck checks if the new TelemetryProvider management API can be used
func (a *AuthAPIClient) TelemetryProviderYBAVersionCheck() (bool, string, error) {
	allowedVersions := YBAMinimumVersion{
		Stable:  util.YBAAllowTelemetryProviderMinStableVersion,
		Preview: util.YBAAllowTelemetryProviderMinPreviewVersion,
	}
	allowed, version, err := a.CheckValidYBAVersion(allowedVersions)
	if err != nil {
		return false, "", err
	}
	return allowed, version, err
}

// ListTelemetryProvidersRest uses REST API to call list schedule functionality
func (a *AuthAPIClient) ListTelemetryProvidersRest(
	callSite,
	operation string,
) (
	[]util.TelemetryProvider, error,
) {
	errorTag := fmt.Errorf("%s, Operation: %s", callSite, operation)

	body, err := a.RestAPICall(
		RestAPIParameters{
			reqBytes:        nil,
			urlRoute:        "telemetry_provider",
			method:          http.MethodGet,
			operationString: "list telemetry providers",
		},
	)
	if err != nil {
		return nil,
			fmt.Errorf("%w: %s", errorTag, err.Error())
	}

	responseBody := []util.TelemetryProvider{}
	if err = json.Unmarshal(body, &responseBody); err != nil {
		return nil,
			fmt.Errorf("%w: Failed unmarshalling list telemetry provider response body %s",
				errorTag,
				err.Error())
	}

	if responseBody != nil {
		return responseBody, nil
	}

	responseBodyError := util.YbaStructuredError{}
	if err = json.Unmarshal(body, &responseBodyError); err != nil {
		return nil,
			fmt.Errorf("%w: Failed unmarshalling list telemetry provider error response body %s",
				errorTag,
				err.Error())
	}

	errorMessage := util.ErrorFromResponseBody(responseBodyError)
	return nil,
		fmt.Errorf("%w: Error fetching list of telemetry providers: %s", errorTag, errorMessage)

}

// CreateTelemetryProviderRest uses REST API to call list schedule functionality
func (a *AuthAPIClient) CreateTelemetryProviderRest(
	reqBody util.TelemetryProvider,
	callSite string,
) (
	util.TelemetryProvider, error,
) {
	errorTag := fmt.Errorf("%s, Operation: Create", callSite)

	reqBytes, err := json.Marshal(reqBody)
	if err != nil {
		return util.TelemetryProvider{},
			fmt.Errorf("%w: %s", errorTag, err.Error())
	}

	body, err := a.RestAPICall(
		RestAPIParameters{
			reqBytes:        reqBytes,
			urlRoute:        "telemetry_provider",
			method:          http.MethodPost,
			operationString: "create telemetry provider",
		},
	)
	if err != nil {
		return util.TelemetryProvider{},
			fmt.Errorf("%w: %s", errorTag, err.Error())
	}

	responseBody := util.TelemetryProvider{}
	if err = json.Unmarshal(body, &responseBody); err != nil {
		return util.TelemetryProvider{},
			fmt.Errorf("%w: Failed unmarshalling create telemetry provider response body %s",
				errorTag,
				err.Error())
	}

	if responseBody.Uuid != "" {
		return responseBody, nil
	}

	responseBodyError := util.YbaStructuredError{}
	if err = json.Unmarshal(body, &responseBodyError); err != nil {
		return util.TelemetryProvider{},
			fmt.Errorf("%w: Failed unmarshalling create telemetry provider error response body %s",
				errorTag,
				err.Error())
	}

	errorMessage := util.ErrorFromResponseBody(responseBodyError)
	return util.TelemetryProvider{},
		fmt.Errorf("%w: Error fetching create of telemetry providers: %s", errorTag, errorMessage)

}
