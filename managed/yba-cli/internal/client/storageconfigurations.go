/*
 * Copyright (c) YugaByte, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// GetListOfCustomerConfig fetches list of configs associated with the customer
func (a *AuthAPIClient) GetListOfCustomerConfig() (
	ybaclient.CustomerConfigurationApiApiGetListOfCustomerConfigRequest) {
	return a.APIClient.CustomerConfigurationApi.GetListOfCustomerConfig(a.ctx, a.CustomerUUID)
}

// DeleteCustomerConfig deletes configs associated with the customer
func (a *AuthAPIClient) DeleteCustomerConfig(configUUID string) (
	ybaclient.CustomerConfigurationApiApiDeleteCustomerConfigRequest) {
	return a.APIClient.CustomerConfigurationApi.DeleteCustomerConfig(
		a.ctx, a.CustomerUUID, configUUID)
}

// CreateCustomerConfig creates configs associated with the customer
func (a *AuthAPIClient) CreateCustomerConfig() (
	ybaclient.CustomerConfigurationApiApiCreateCustomerConfigRequest) {
	return a.APIClient.CustomerConfigurationApi.CreateCustomerConfig(a.ctx, a.CustomerUUID)
}

// EditCustomerConfig edits configs associated with the customer
func (a *AuthAPIClient) EditCustomerConfig(configUUID string) (
	ybaclient.CustomerConfigurationApiApiEditCustomerConfigRequest) {
	return a.APIClient.CustomerConfigurationApi.EditCustomerConfig(
		a.ctx,
		a.CustomerUUID,
		configUUID,
	)
}
