/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// CustomerDetail fetches customer details
func (a *AuthAPIClient) CustomerDetail() ybaclient.CustomerManagementAPICustomerDetailRequest {
	return a.APIClient.CustomerManagementAPI.CustomerDetail(a.ctx, a.CustomerUUID)
}

// UpdateCustomer fetches update customer
func (a *AuthAPIClient) UpdateCustomer() ybaclient.CustomerManagementAPIUpdateCustomerRequest {
	return a.APIClient.CustomerManagementAPI.UpdateCustomer(a.ctx, a.CustomerUUID)
}

// ListOfCustomers fetches list customer
func (a *AuthAPIClient) ListOfCustomers() ybaclient.CustomerManagementAPIListOfCustomersRequest {
	return a.APIClient.CustomerManagementAPI.ListOfCustomers(a.ctx)
}
