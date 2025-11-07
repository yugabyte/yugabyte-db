/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// CustomerDetail fetches customer details
func (a *AuthAPIClient) CustomerDetail() ybaclient.CustomerManagementApiApiCustomerDetailRequest {
	return a.APIClient.CustomerManagementApi.CustomerDetail(a.ctx, a.CustomerUUID)
}

// UpdateCustomer fetches update customer
func (a *AuthAPIClient) UpdateCustomer() ybaclient.CustomerManagementApiApiUpdateCustomerRequest {
	return a.APIClient.CustomerManagementApi.UpdateCustomer(a.ctx, a.CustomerUUID)
}

// ListOfCustomers fetches list customer
func (a *AuthAPIClient) ListOfCustomers() ybaclient.CustomerManagementApiApiListOfCustomersRequest {
	return a.APIClient.CustomerManagementApi.ListOfCustomers(a.ctx)
}
