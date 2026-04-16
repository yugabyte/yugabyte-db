/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	"bytes"
	"fmt"
	"io"
	"net/http"

	"github.com/spf13/viper"
)

// RestAPIParameters is a struct to hold the parameters for a REST API call
type RestAPIParameters struct {
	reqBytes        []byte
	method          string
	urlRoute        string
	operationString string
}

// RestAPICall makes a REST API call to the YW API
func (a *AuthAPIClient) RestAPICall(
	params RestAPIParameters,
) ([]byte, error) {
	token := viper.GetString("apiToken")

	reqBuf := bytes.NewBuffer(params.reqBytes)

	req, err := http.NewRequest(
		params.method,
		fmt.Sprintf("%s://%s/api/v1/customers/%s/%s",
			a.RestClient.Scheme, a.RestClient.Host, a.CustomerUUID, params.urlRoute),
		reqBuf,
	)

	if err != nil {
		return nil, err
	}

	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("X-AUTH-YW-API-TOKEN", token)

	r, err := a.RestClient.Client.Do(req)
	if err != nil {
		return nil,
			fmt.Errorf("Error occured during %s call for %s %s",
				params.method,
				params.operationString,
				err.Error())
	}

	var body []byte
	body, err = io.ReadAll(r.Body)
	if err != nil {
		return nil,
			fmt.Errorf("Error reading %s response body %s",
				params.operationString,
				err.Error())
	}

	return body, nil
}

// RestAPICallV1Path makes a REST call to /api/v1/{path}
// Use for endpoints that are not under customers.
func (a *AuthAPIClient) RestAPICallV1Path(
	params RestAPIParameters,
) ([]byte, error) {
	token := viper.GetString("apiToken")

	reqBuf := bytes.NewBuffer(params.reqBytes)

	req, err := http.NewRequest(
		params.method,
		fmt.Sprintf("%s://%s/api/v1/%s",
			a.RestClient.Scheme, a.RestClient.Host, params.urlRoute),
		reqBuf,
	)
	if err != nil {
		return nil, err
	}

	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("X-AUTH-YW-API-TOKEN", token)

	r, err := a.RestClient.Client.Do(req)
	if err != nil {
		return nil, fmt.Errorf("Error occured during %s call for %s %s",
			params.method, params.operationString, err.Error())
	}

	body, err := io.ReadAll(r.Body)
	defer r.Body.Close()

	if err != nil {
		return nil, fmt.Errorf("error reading %s: %w", params.operationString, err)
	}

	return body, nil
}
