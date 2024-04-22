/*
 * Copyright (c) YugaByte, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// CreateNodeInstance adds a node instance to an onprem provider
func (a *AuthAPIClient) CreateNodeInstance(azUUID string) (
	ybaclient.NodeInstancesApiApiCreateNodeInstanceRequest,
) {
	return a.APIClient.NodeInstancesApi.CreateNodeInstance(a.ctx, a.CustomerUUID, azUUID)
}

// ListByProvider fetches node instances associated to an onprem provider
func (a *AuthAPIClient) ListByProvider(pUUID string) (
	ybaclient.NodeInstancesApiApiListByProviderRequest,
) {
	return a.APIClient.NodeInstancesApi.ListByProvider(a.ctx, a.CustomerUUID, pUUID)
}

// DeleteInstance deletes the node instance from the onprem provider
func (a *AuthAPIClient) DeleteInstance(pUUID, ip string) (
	ybaclient.NodeInstancesApiApiDeleteInstanceRequest,
) {
	return a.APIClient.NodeInstancesApi.DeleteInstance(a.ctx, a.CustomerUUID, pUUID, ip)
}

// DetachedNodeAction deletes the node instance from the onprem provider
func (a *AuthAPIClient) DetachedNodeAction(pUUID, ip string) (
	ybaclient.NodeInstancesApiApiDetachedNodeActionRequest,
) {
	return a.APIClient.NodeInstancesApi.DetachedNodeAction(a.ctx, a.CustomerUUID, pUUID, ip)
}

// GetNodeInstance fetches a node based on UUID
func (a *AuthAPIClient) GetNodeInstance(nUUID string) (
	ybaclient.NodeInstancesApiApiGetNodeInstanceRequest,
) {
	return a.APIClient.NodeInstancesApi.GetNodeInstance(a.ctx, a.CustomerUUID, nUUID)
}

// GetNodeDetails fetches a node based on UUID
func (a *AuthAPIClient) GetNodeDetails(uUUID, nName string) (
	ybaclient.NodeInstancesApiApiGetNodeDetailsRequest) {
	return a.APIClient.NodeInstancesApi.GetNodeDetails(a.ctx, a.CustomerUUID, uUUID, nName)
}

// NodeAction for the node operations for universes
func (a *AuthAPIClient) NodeAction(uUUID, nodeName string) (
	ybaclient.NodeInstancesApiApiNodeActionRequest) {
	return a.APIClient.NodeInstancesApi.NodeAction(a.ctx, a.CustomerUUID, uUUID, nodeName)
}
