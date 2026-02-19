/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// CreateXClusterConfig create xCluster config
func (a *AuthAPIClient) CreateXClusterConfig() ybaclient.AsynchronousReplicationAPICreateXClusterConfigRequest {
	return a.APIClient.AsynchronousReplicationAPI.CreateXClusterConfig(
		a.ctx,
		a.CustomerUUID)
}

// DeleteXClusterConfig delete xCluster config
func (a *AuthAPIClient) DeleteXClusterConfig(
	xclusterUUID string,
) ybaclient.AsynchronousReplicationAPIDeleteXClusterConfigRequest {
	return a.APIClient.AsynchronousReplicationAPI.DeleteXClusterConfig(
		a.ctx,
		a.CustomerUUID,
		xclusterUUID)
}

// GetXClusterConfig get xCluster config
func (a *AuthAPIClient) GetXClusterConfig(
	xclusterUUID string,
) ybaclient.AsynchronousReplicationAPIGetXClusterConfigRequest {
	return a.APIClient.AsynchronousReplicationAPI.GetXClusterConfig(
		a.ctx,
		a.CustomerUUID,
		xclusterUUID)
}

// EditXClusterConfig edit xCluster config
func (a *AuthAPIClient) EditXClusterConfig(
	xclusterUUID string,
) ybaclient.AsynchronousReplicationAPIEditXClusterConfigRequest {
	return a.APIClient.AsynchronousReplicationAPI.EditXClusterConfig(
		a.ctx,
		a.CustomerUUID,
		xclusterUUID)
}

// RestartXClusterConfig restart xCluster
func (a *AuthAPIClient) RestartXClusterConfig(
	xclusterUUID string,
) ybaclient.AsynchronousReplicationAPIRestartXClusterConfigRequest {
	return a.APIClient.AsynchronousReplicationAPI.RestartXClusterConfig(
		a.ctx,
		a.CustomerUUID,
		xclusterUUID)
}

// SyncXClusterConfig sync xCluster
func (a *AuthAPIClient) SyncXClusterConfig(
	xclusterUUID string,
) ybaclient.AsynchronousReplicationAPISyncXClusterConfigV2Request {
	return a.APIClient.AsynchronousReplicationAPI.SyncXClusterConfigV2(
		a.ctx,
		a.CustomerUUID,
		xclusterUUID)
}

// NeedBootstrapTable need bootstrap table
func (a *AuthAPIClient) NeedBootstrapTable(
	universeUUID string,
) ybaclient.AsynchronousReplicationAPINeedBootstrapTableRequest {
	return a.APIClient.AsynchronousReplicationAPI.NeedBootstrapTable(
		a.ctx,
		a.CustomerUUID,
		universeUUID)
}

// NeedBootstrapXClusterConfig need bootstrap xCluster
func (a *AuthAPIClient) NeedBootstrapXClusterConfig(
	xclusterUUID string,
) ybaclient.AsynchronousReplicationAPINeedBootstrapXClusterConfigRequest {
	return a.APIClient.AsynchronousReplicationAPI.NeedBootstrapXClusterConfig(
		a.ctx,
		a.CustomerUUID,
		xclusterUUID)
}
