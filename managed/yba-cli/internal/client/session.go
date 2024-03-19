/*
 * Copyright (c) YugaByte, Inc.
 */

package client

import (
	"errors"

	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// GetAppVersion fetches YugabyteDB Anywhere version
func (a *AuthAPIClient) GetAppVersion() ybaclient.SessionManagementApiApiAppVersionRequest {
	return a.APIClient.SessionManagementApi.AppVersion(a.ctx)
}

// ApiLogin fetches API Token and CustomerUUID
func (a *AuthAPIClient) ApiLogin() ybaclient.SessionManagementApiApiApiLoginRequest {
	return a.APIClient.SessionManagementApi.ApiLogin(a.ctx)
}

// GetSessionInfo fetches YugabyteDB Anywhere session info
func (a *AuthAPIClient) GetSessionInfo() (
	ybaclient.SessionManagementApiApiGetSessionInfoRequest) {
	return a.APIClient.SessionManagementApi.GetSessionInfo(a.ctx)
}

// GetCustomerUUID fetches YugabyteDB Anywhere customer UUID
func (a *AuthAPIClient) GetCustomerUUID() error {
	r, response, err := a.GetSessionInfo().Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(response, err,
			"Get Customer UUID", "Get Session Info")
		return errMessage
	}
	if !r.HasCustomerUUID() {
		err := "could not retrieve Customer UUID"
		return errors.New(err)
	}
	a.CustomerUUID = r.GetCustomerUUID()
	return nil
}
