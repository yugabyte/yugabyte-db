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
	ybaclient.UniverseUpgradesManagementApiApiUpgradeSoftwareRequest,
) {
	return a.APIClient.UniverseUpgradesManagementApi.UpgradeSoftware(a.ctx, a.CustomerUUID, uUUID)
}

// UniverseYBAVersionCheck checks if the new API request body can be used for the Create
// Provider API
func (a *AuthAPIClient) UniverseYBAVersionCheck() (bool, string, error) {
	allowedVersions := []string{util.YBAAllowUniverseMinVersion}
	allowed, version, err := a.CheckValidYBAVersion(allowedVersions)
	if err != nil {
		return false, "", err
	}
	return allowed, version, err
}
