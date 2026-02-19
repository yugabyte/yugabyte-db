/*
 * Copyright (c) YugabyteDB, Inc.
 */

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

// GetHAConfig fetches HA configuration
func (a *AuthAPIClient) GetHAConfig() ybaclient.HAAPIGetHAConfigRequest {
	return a.APIClient.HAAPI.GetHAConfig(a.ctx)
}

// CreateHAConfig creates a new HA configuration
func (a *AuthAPIClient) CreateHAConfig() ybaclient.HAAPICreateHAConfigRequest {
	return a.APIClient.HAAPI.CreateHAConfig(a.ctx)
}

// UpdateHAConfigRest uses REST API to update HA configuration (sends body correctly).
func (a *AuthAPIClient) UpdateHAConfigRest(
	uuid string,
	callSite string,
	reqBody ybaclient.HAConfigFormData,
) (*ybaclient.HighAvailabilityConfig, error) {
	errorTag := fmt.Errorf("%s, Operation: Update", callSite)

	reqBytes, err := json.Marshal(reqBody)
	if err != nil {
		return nil, fmt.Errorf("%w: %s", errorTag, err.Error())
	}

	body, err := a.RestAPICallV1Path(
		RestAPIParameters{
			reqBytes:        reqBytes,
			method:          http.MethodPut,
			urlRoute:        fmt.Sprintf("settings/ha/config/%s", uuid),
			operationString: "update HA config",
		},
	)
	if err != nil {
		return nil, fmt.Errorf("%w: %s", errorTag, err.Error())
	}

	responseBody := ybaclient.HighAvailabilityConfig{}
	if err = json.Unmarshal(body, &responseBody); err != nil {
		// Server may have returned an error payload (e.g. 4xx); try to surface it
		responseBodyError := util.YbaStructuredError{}
		if parseErr := json.Unmarshal(body, &responseBodyError); parseErr == nil {
			errorMessage := util.ErrorFromResponseBody(responseBodyError)
			return nil, fmt.Errorf("%w: %s", errorTag, errorMessage)
		}
		return nil, fmt.Errorf("%w: Failed unmarshalling update HA config response: %s",
			errorTag, err.Error())
	}

	if responseBody.Uuid != "" {
		return &responseBody, nil
	}

	responseBodyError := util.YbaStructuredError{}
	if err = json.Unmarshal(body, &responseBodyError); err != nil {
		return nil, fmt.Errorf("%w: Failed unmarshalling error response: %s",
			errorTag, err.Error())
	}
	errorMessage := util.ErrorFromResponseBody(responseBodyError)
	return nil, fmt.Errorf("%w: Error updating HA config: %s", errorTag, errorMessage)
}

// DeleteHAConfig deletes an HA configuration
func (a *AuthAPIClient) DeleteHAConfig(uuid string) ybaclient.HAAPIDeleteHAConfigRequest {
	return a.APIClient.HAAPI.DeleteHAConfig(a.ctx, uuid)
}

// GenerateClusterKey generates a new cluster key
func (a *AuthAPIClient) GenerateClusterKey() ybaclient.HAAPIGenerateClusterKeyRequest {
	return a.APIClient.HAAPI.GenerateClusterKey(a.ctx)
}
