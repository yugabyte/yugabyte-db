/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// ListPITRConfig for the listing of all pitr configs in the universe
func (a *AuthAPIClient) ListPITRConfig(
	uUUID string,
) ybaclient.PITRManagementAPIListOfPitrConfigsRequest {
	return a.APIClient.PITRManagementAPI.ListOfPitrConfigs(a.ctx, a.CustomerUUID, uUUID)
}

// CreatePITRConfig for the creation of a new pitr config in the universe
func (a *AuthAPIClient) CreatePITRConfig(
	uUUID string,
	tableType string,
	keyspace string,
) ybaclient.PITRManagementAPICreatePitrConfigRequest {
	return a.APIClient.PITRManagementAPI.CreatePitrConfig(
		a.ctx,
		a.CustomerUUID,
		uUUID,
		tableType,
		keyspace,
	)
}

// UpdatePITRConfig for the update of a pitr config in the universe
func (a *AuthAPIClient) UpdatePITRConfig(
	uUUID string,
	configUUID string,
) ybaclient.PITRManagementAPIUpdatePitrConfigRequest {
	return a.APIClient.PITRManagementAPI.UpdatePitrConfig(a.ctx, a.CustomerUUID, uUUID, configUUID)
}

// DeletePITRConfig for the deletion of a pitr config in the universe
func (a *AuthAPIClient) DeletePITRConfig(
	uUUID string,
	configUUID string,
) ybaclient.PITRManagementAPIDeletePitrConfigRequest {
	return a.APIClient.PITRManagementAPI.DeletePitrConfig(a.ctx, a.CustomerUUID, uUUID, configUUID)
}

// PerformPITR for point in time recovery of the keyspace in the universe
func (a *AuthAPIClient) PerformPITR(uUUID string) ybaclient.PITRManagementAPIPerformPitrRequest {
	return a.APIClient.PITRManagementAPI.PerformPitr(a.ctx, a.CustomerUUID, uUUID)
}
