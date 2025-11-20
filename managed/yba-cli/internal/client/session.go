/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	"errors"

	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// GetAppVersion fetches YugabyteDB Anywhere version
func (a *AuthAPIClient) GetAppVersion() ybaclient.SessionManagementAPIAppVersionRequest {
	return a.APIClient.SessionManagementAPI.AppVersion(a.ctx)
}

// ApiLogin fetches API Token and CustomerUUID
func (a *AuthAPIClient) ApiLogin() ybaclient.SessionManagementAPIApiLoginRequest {
	return a.APIClient.SessionManagementAPI.ApiLogin(a.ctx)
}

// RegisterCustomer registers a YugabyteDB Anywhere customer
func (a *AuthAPIClient) RegisterCustomer() ybaclient.SessionManagementAPIRegisterCustomerRequest {
	return a.APIClient.SessionManagementAPI.RegisterCustomer(a.ctx)
}

// ApiToken regenerates and fetches unmasked API token
func (a *AuthAPIClient) ApiToken() ybaclient.SessionManagementAPIApiTokenRequest {
	return a.APIClient.SessionManagementAPI.ApiToken(a.ctx, a.CustomerUUID)
}

// GetSessionInfo fetches YugabyteDB Anywhere session info
func (a *AuthAPIClient) GetSessionInfo() ybaclient.SessionManagementAPIGetSessionInfoRequest {
	return a.APIClient.SessionManagementAPI.GetSessionInfo(a.ctx)
}

// GetCustomerUUID fetches YugabyteDB Anywhere customer UUID
func (a *AuthAPIClient) GetCustomerUUID() error {
	r, response, err := a.GetSessionInfo().Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(response, err,
			"Get Session Info", "Get Customer UUID")
		return errMessage
	}
	if !r.HasCustomerUUID() {
		err := "could not retrieve Customer UUID"
		return errors.New(err)
	}
	a.CustomerUUID = r.GetCustomerUUID()
	return nil
}
