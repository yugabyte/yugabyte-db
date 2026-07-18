/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// CreateNodeInstance adds a node instance to an onprem provider
func (a *AuthAPIClient) CreateNodeInstance(
	azUUID string,
) ybaclient.NodeInstancesAPICreateNodeInstanceRequest {
	return a.APIClient.NodeInstancesAPI.CreateNodeInstance(a.ctx, a.CustomerUUID, azUUID)
}

// ListByProvider fetches node instances associated to an onprem provider
func (a *AuthAPIClient) ListByProvider(
	pUUID string,
) ybaclient.NodeInstancesAPIListByProviderRequest {
	return a.APIClient.NodeInstancesAPI.ListByProvider(a.ctx, a.CustomerUUID, pUUID)
}

// DeleteInstance deletes the node instance from the onprem provider
func (a *AuthAPIClient) DeleteInstance(
	pUUID, ip string,
) ybaclient.NodeInstancesAPIDeleteInstanceRequest {
	return a.APIClient.NodeInstancesAPI.DeleteInstance(a.ctx, a.CustomerUUID, pUUID, ip)
}

// DetachedNodeAction deletes the node instance from the onprem provider
func (a *AuthAPIClient) DetachedNodeAction(
	pUUID, ip string,
) ybaclient.NodeInstancesAPIDetachedNodeActionRequest {
	return a.APIClient.NodeInstancesAPI.DetachedNodeAction(a.ctx, a.CustomerUUID, pUUID, ip)
}

// GetNodeInstance fetches a node based on UUID
func (a *AuthAPIClient) GetNodeInstance(
	nUUID string,
) ybaclient.NodeInstancesAPIGetNodeInstanceRequest {
	return a.APIClient.NodeInstancesAPI.GetNodeInstance(a.ctx, a.CustomerUUID, nUUID)
}

// GetNodeDetails fetches a node based on UUID
func (a *AuthAPIClient) GetNodeDetails(
	uUUID, nName string,
) ybaclient.NodeInstancesAPIGetNodeDetailsRequest {
	return a.APIClient.NodeInstancesAPI.GetNodeDetails(a.ctx, a.CustomerUUID, uUUID, nName)
}

// NodeAction for the node operations for universes
func (a *AuthAPIClient) NodeAction(
	uUUID, nodeName string,
) ybaclient.NodeInstancesAPINodeActionRequest {
	return a.APIClient.NodeInstancesAPI.NodeAction(a.ctx, a.CustomerUUID, uUUID, nodeName)
}
