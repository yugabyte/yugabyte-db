/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// GetListOfCustomerConfig fetches list of configs associated with the customer
func (a *AuthAPIClient) GetListOfCustomerConfig() ybaclient.CustomerConfigurationAPIGetListOfCustomerConfigRequest {
	return a.APIClient.CustomerConfigurationAPI.GetListOfCustomerConfig(a.ctx, a.CustomerUUID)
}

// DeleteCustomerConfig deletes configs associated with the customer
func (a *AuthAPIClient) DeleteCustomerConfig(
	configUUID string,
) ybaclient.CustomerConfigurationAPIDeleteCustomerConfigRequest {
	return a.APIClient.CustomerConfigurationAPI.DeleteCustomerConfig(
		a.ctx, a.CustomerUUID, configUUID)
}

// CreateCustomerConfig creates configs associated with the customer
func (a *AuthAPIClient) CreateCustomerConfig() ybaclient.CustomerConfigurationAPICreateCustomerConfigRequest {
	return a.APIClient.CustomerConfigurationAPI.CreateCustomerConfig(a.ctx, a.CustomerUUID)
}

// EditCustomerConfig edits configs associated with the customer
func (a *AuthAPIClient) EditCustomerConfig(
	configUUID string,
) ybaclient.CustomerConfigurationAPIEditCustomerConfigRequest {
	return a.APIClient.CustomerConfigurationAPI.EditCustomerConfig(
		a.ctx,
		a.CustomerUUID,
		configUUID,
	)
}
