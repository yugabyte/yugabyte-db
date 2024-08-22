/*
 * Copyright (c) YugaByte, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// ListUniverses fetches list of universes associated with the customer
func (a *AuthAPIClient) ListUniverses() (
	ybaclient.UniverseManagementApiApiListUniversesRequest) {
	return a.APIClient.UniverseManagementApi.ListUniverses(a.ctx, a.CustomerUUID)
}

// GetUniverse fetches of universe associated with the universeUUID
func (a *AuthAPIClient) GetUniverse(uUUID string) (
	ybaclient.UniverseManagementApiApiGetUniverseRequest) {
	return a.APIClient.UniverseManagementApi.GetUniverse(a.ctx, a.CustomerUUID, uUUID)
}

// DeleteUniverse deletes universe associated with the universeUUID
func (a *AuthAPIClient) DeleteUniverse(uUUID string) (
	ybaclient.UniverseManagementApiApiDeleteUniverseRequest) {
	return a.APIClient.UniverseManagementApi.DeleteUniverse(a.ctx, a.CustomerUUID, uUUID)
}

// CreateAllClusters creates a universe with a minimum of 1 cluster
func (a *AuthAPIClient) CreateAllClusters() (
	ybaclient.UniverseClusterMutationsApiApiCreateAllClustersRequest) {
	return a.APIClient.UniverseClusterMutationsApi.CreateAllClusters(a.ctx, a.CustomerUUID)
}

// UpgradeSoftware upgrades the universe YugabyteDB version
func (a *AuthAPIClient) UpgradeSoftware(uUUID string) (
	ybaclient.UniverseUpgradesManagementApiApiUpgradeSoftwareRequest) {
	return a.APIClient.UniverseUpgradesManagementApi.UpgradeSoftware(a.ctx, a.CustomerUUID, uUUID)
}

// UpgradeGFlags upgrades the universe gflags
func (a *AuthAPIClient) UpgradeGFlags(uUUID string) (
	ybaclient.UniverseUpgradesManagementApiApiUpgradeGFlagsRequest) {
	return a.APIClient.UniverseUpgradesManagementApi.UpgradeGFlags(a.ctx, a.CustomerUUID, uUUID)
}

// UpgradeVMImage upgrades the VM image of the universe
func (a *AuthAPIClient) UpgradeVMImage(uUUID string) (
	ybaclient.UniverseUpgradesManagementApiApiUpgradeVMImageRequest) {
	return a.APIClient.UniverseUpgradesManagementApi.UpgradeVMImage(a.ctx, a.CustomerUUID, uUUID)
}

// RestartUniverse for restart operation
func (a *AuthAPIClient) RestartUniverse(uUUID string) (
	ybaclient.UniverseUpgradesManagementApiApiRestartUniverseRequest) {
	return a.APIClient.UniverseUpgradesManagementApi.RestartUniverse(a.ctx, a.CustomerUUID, uUUID)
}

// UniverseYBAVersionCheck checks if the new API request body can be used for the Create
// Provider API
func (a *AuthAPIClient) UniverseYBAVersionCheck() (bool, string, error) {
	allowedVersions := YBAMinimumVersion{
		Stable:  util.YBAAllowUniverseMinVersion,
		Preview: util.YBAAllowUniverseMinVersion,
	}
	allowed, version, err := a.CheckValidYBAVersion(allowedVersions)
	if err != nil {
		return false, "", err
	}
	return allowed, version, err
}
