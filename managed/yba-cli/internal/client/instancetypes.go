/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// ListOfInstanceType fetches the list of instance types
// associated with the provider (for onprem)
func (a *AuthAPIClient) ListOfInstanceType(
	pUUID string,
) ybaclient.InstanceTypesAPIListOfInstanceTypeRequest {
	return a.APIClient.InstanceTypesAPI.ListOfInstanceType(a.ctx, a.CustomerUUID, pUUID)
}

// CreateInstanceType for onprem providers
func (a *AuthAPIClient) CreateInstanceType(
	pUUID string,
) ybaclient.InstanceTypesAPICreateInstanceTypeRequest {
	return a.APIClient.InstanceTypesAPI.CreateInstanceType(a.ctx, a.CustomerUUID, pUUID)
}

// DeleteInstanceType for onprem providers
func (a *AuthAPIClient) DeleteInstanceType(
	pUUID, instanceTypeName string,
) ybaclient.InstanceTypesAPIDeleteInstanceTypeRequest {
	return a.APIClient.InstanceTypesAPI.DeleteInstanceType(a.ctx,
		a.CustomerUUID, pUUID, instanceTypeName)
}

// InstanceTypeDetail fetches details of the instance type in an onprem provider
func (a *AuthAPIClient) InstanceTypeDetail(
	pUUID, instanceTypeName string,
) ybaclient.InstanceTypesAPIInstanceTypeDetailRequest {
	return a.APIClient.InstanceTypesAPI.InstanceTypeDetail(a.ctx,
		a.CustomerUUID, pUUID, instanceTypeName)
}
