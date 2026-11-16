/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"strings"

	"github.com/spf13/viper"

	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// RestAPIParameters is a struct to hold the parameters for a REST API call
type RestAPIParameters struct {
	reqBytes        []byte
	method          string
	urlRoute        string
	operationString string
}

// restAPICall issues params against the given base path - "api/v2/customers/<uuid>" and the like.
// prefix is an argument rather than a RestAPIParameters field so a call site cannot pick it.
// A non-2xx is an error here: an error body decodes cleanly into a zero value because every field
// is omitempty, so without the check an expired token reads as an empty result, not a failure.
func (a *AuthAPIClient) restAPICall(
	prefix string,
	params RestAPIParameters,
) ([]byte, error) {
	token := viper.GetString("apiToken")

	reqBuf := bytes.NewBuffer(params.reqBytes)

	req, err := http.NewRequest(
		params.method,
		fmt.Sprintf("%s://%s/%s/%s",
			a.RestClient.Scheme, a.RestClient.Host, prefix, params.urlRoute),
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
	defer r.Body.Close()

	body, err := io.ReadAll(r.Body)
	if err != nil {
		return nil,
			fmt.Errorf("Error reading %s response body %s",
				params.operationString,
				err.Error())
	}

	if r.StatusCode < http.StatusOK || r.StatusCode >= http.StatusMultipleChoices {
		return nil, fmt.Errorf("%s failed with %s: %s",
			params.operationString, r.Status, errorMessageFromBody(body))
	}

	return body, nil
}

// errorMessageFromBody renders the server's own message when the body carries one, and falls back
// to the raw body. The Error nil check is load-bearing: util.ErrorFromResponseBody dereferences
// YbaStructuredError.Error without one and panics on a body that has no error key.
func errorMessageFromBody(body []byte) string {
	responseBodyError := util.YbaStructuredError{}
	if err := json.Unmarshal(body, &responseBodyError); err == nil &&
		responseBodyError.Error != nil {
		// Trimmed: ErrorFromResponseBody prepends a space for the structured form.
		return strings.TrimSpace(util.ErrorFromResponseBody(responseBodyError))
	}
	trimmed := string(body)
	if len(trimmed) > 512 {
		trimmed = trimmed[:512] + "..."
	}
	return trimmed
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
	req.Header.Set("User-Agent", UserAgent())

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

// RestAPICallV2 makes a REST API call to the YBA v2 API under /api/v2/customers/{cUUID}/.
// Used by the support bundle v2 paged-list calls, which cannot go through the generated client
// (see internal/client/supportbundlev2.go).
func (a *AuthAPIClient) RestAPICallV2(
	params RestAPIParameters,
) ([]byte, error) {
	return a.restAPICall(fmt.Sprintf("api/v2/customers/%s", a.CustomerUUID), params)
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
	req.Header.Set("User-Agent", UserAgent())

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
