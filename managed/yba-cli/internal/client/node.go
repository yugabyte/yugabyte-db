/*
 * Copyright (c) YugaByte, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

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
