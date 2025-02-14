/*
 * Copyright (c) YugaByte, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// CreateXClusterConfig create xCluster config
func (a *AuthAPIClient) CreateXClusterConfig() (
	ybaclient.AsynchronousReplicationApiApiCreateXClusterConfigRequest) {
	return a.APIClient.AsynchronousReplicationApi.CreateXClusterConfig(
		a.ctx,
		a.CustomerUUID)
}

// DeleteXClusterConfig delete xCluster config
func (a *AuthAPIClient) DeleteXClusterConfig(xclusterUUID string) (
	ybaclient.AsynchronousReplicationApiApiDeleteXClusterConfigRequest) {
	return a.APIClient.AsynchronousReplicationApi.DeleteXClusterConfig(
		a.ctx,
		a.CustomerUUID,
		xclusterUUID)
}

// GetXClusterConfig get xCluster config
func (a *AuthAPIClient) GetXClusterConfig(xclusterUUID string) (
	ybaclient.AsynchronousReplicationApiApiGetXClusterConfigRequest) {
	return a.APIClient.AsynchronousReplicationApi.GetXClusterConfig(
		a.ctx,
		a.CustomerUUID,
		xclusterUUID)
}

// EditXClusterConfig edit xCluster config
func (a *AuthAPIClient) EditXClusterConfig(xclusterUUID string) (
	ybaclient.AsynchronousReplicationApiApiEditXClusterConfigRequest) {
	return a.APIClient.AsynchronousReplicationApi.EditXClusterConfig(
		a.ctx,
		a.CustomerUUID,
		xclusterUUID)
}

// RestartXClusterConfig restart xCluster
func (a *AuthAPIClient) RestartXClusterConfig(xclusterUUID string) (
	ybaclient.AsynchronousReplicationApiApiRestartXClusterConfigRequest) {
	return a.APIClient.AsynchronousReplicationApi.RestartXClusterConfig(
		a.ctx,
		a.CustomerUUID,
		xclusterUUID)
}

// SyncXClusterConfig sync xCluster
func (a *AuthAPIClient) SyncXClusterConfig(xclusterUUID string) (
	ybaclient.AsynchronousReplicationApiApiSyncXClusterConfigV2Request) {
	return a.APIClient.AsynchronousReplicationApi.SyncXClusterConfigV2(
		a.ctx,
		a.CustomerUUID,
		xclusterUUID)
}

// NeedBootstrapTable need bootstrap table
func (a *AuthAPIClient) NeedBootstrapTable(universeUUID string) (
	ybaclient.AsynchronousReplicationApiApiNeedBootstrapTableRequest) {
	return a.APIClient.AsynchronousReplicationApi.NeedBootstrapTable(
		a.ctx,
		a.CustomerUUID,
		universeUUID)
}

// NeedBootstrapXClusterConfig need bootstrap xCluster
func (a *AuthAPIClient) NeedBootstrapXClusterConfig(xclusterUUID string) (
	ybaclient.AsynchronousReplicationApiApiNeedBootstrapXClusterConfigRequest) {
	return a.APIClient.AsynchronousReplicationApi.NeedBootstrapXClusterConfig(
		a.ctx,
		a.CustomerUUID,
		xclusterUUID)
}
