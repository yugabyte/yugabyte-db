/*
 * Copyright (c) YugaByte, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// ListOfInstanceType fetches the list of instance types
// associated with the provider (for onprem)
func (a *AuthAPIClient) ListOfInstanceType(pUUID string) (
	ybaclient.InstanceTypesApiApiListOfInstanceTypeRequest,
) {
	return a.APIClient.InstanceTypesApi.ListOfInstanceType(a.ctx, a.CustomerUUID, pUUID)
}

// CreateInstanceType for onprem providers
func (a *AuthAPIClient) CreateInstanceType(pUUID string) (
	ybaclient.InstanceTypesApiApiCreateInstanceTypeRequest,
) {
	return a.APIClient.InstanceTypesApi.CreateInstanceType(a.ctx, a.CustomerUUID, pUUID)
}

// DeleteInstanceType for onprem providers
func (a *AuthAPIClient) DeleteInstanceType(pUUID, instanceTypeName string) (
	ybaclient.InstanceTypesApiApiDeleteInstanceTypeRequest,
) {
	return a.APIClient.InstanceTypesApi.DeleteInstanceType(a.ctx,
		a.CustomerUUID, pUUID, instanceTypeName)
}

// InstanceTypeDetail fetches details of the instance type in an onprem provider
func (a *AuthAPIClient) InstanceTypeDetail(pUUID, instanceTypeName string) (
	ybaclient.InstanceTypesApiApiInstanceTypeDetailRequest,
) {
	return a.APIClient.InstanceTypesApi.InstanceTypeDetail(a.ctx,
		a.CustomerUUID, pUUID, instanceTypeName)
}
