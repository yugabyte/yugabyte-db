/*
 * Copyright (c) YugaByte, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// ListUniverses fetches list of universes associated with the customer
func (a *AuthAPIClient) ListUniverses() ybaclient.UniverseManagementApiApiListUniversesRequest {
	return a.APIClient.UniverseManagementApi.ListUniverses(a.ctx, a.CustomerUUID)
}

// GetUniverse fetches of universe associated with the universeUUID
func (a *AuthAPIClient) GetUniverse(
	uUUID string,
) ybaclient.UniverseManagementApiApiGetUniverseRequest {
	return a.APIClient.UniverseManagementApi.GetUniverse(a.ctx, a.CustomerUUID, uUUID)
}

// DeleteUniverse deletes universe associated with the universeUUID
func (a *AuthAPIClient) DeleteUniverse(
	uUUID string,
) ybaclient.UniverseManagementApiApiDeleteUniverseRequest {
	return a.APIClient.UniverseManagementApi.DeleteUniverse(a.ctx, a.CustomerUUID, uUUID)
}

// CreateAllClusters creates a universe with a minimum of 1 cluster
func (a *AuthAPIClient) CreateAllClusters() ybaclient.UniverseClusterMutationsApiApiCreateAllClustersRequest {
	return a.APIClient.UniverseClusterMutationsApi.CreateAllClusters(a.ctx, a.CustomerUUID)
}

// DeleteReadonlyCluster to remove read replica cluster
func (a *AuthAPIClient) DeleteReadonlyCluster(
	uUUID, clusterUUID string,
) ybaclient.UniverseClusterMutationsApiApiDeleteReadonlyClusterRequest {
	return a.APIClient.UniverseClusterMutationsApi.DeleteReadonlyCluster(
		a.ctx, a.CustomerUUID, uUUID, clusterUUID)
}

// CreateReadOnlyCluster to create a read replica cluster
func (a *AuthAPIClient) CreateReadOnlyCluster(
	uUUID string,
) ybaclient.UniverseClusterMutationsApiApiCreateReadOnlyClusterRequest {
	return a.APIClient.UniverseClusterMutationsApi.CreateReadOnlyCluster(
		a.ctx, a.CustomerUUID, uUUID)
}

// UpgradeSoftware upgrades the universe YugabyteDB version
func (a *AuthAPIClient) UpgradeSoftware(
	uUUID string,
) ybaclient.UniverseUpgradesManagementApiApiUpgradeSoftwareRequest {
	return a.APIClient.UniverseUpgradesManagementApi.UpgradeSoftware(a.ctx, a.CustomerUUID, uUUID)
}

// UpdatePrimaryCluster to edit primary cluster components
func (a *AuthAPIClient) UpdatePrimaryCluster(
	uUUID string,
) ybaclient.UniverseClusterMutationsApiApiUpdatePrimaryClusterRequest {
	return a.APIClient.UniverseClusterMutationsApi.UpdatePrimaryCluster(
		a.ctx, a.CustomerUUID, uUUID)
}

// UpdateReadOnlyCluster to edit read replica cluster components
func (a *AuthAPIClient) UpdateReadOnlyCluster(
	uUUID string,
) ybaclient.UniverseClusterMutationsApiApiUpdateReadOnlyClusterRequest {
	return a.APIClient.UniverseClusterMutationsApi.UpdateReadOnlyCluster(
		a.ctx, a.CustomerUUID, uUUID)
}

// UpgradeGFlags upgrades the universe gflags
func (a *AuthAPIClient) UpgradeGFlags(
	uUUID string,
) ybaclient.UniverseUpgradesManagementApiApiUpgradeGFlagsRequest {
	return a.APIClient.UniverseUpgradesManagementApi.UpgradeGFlags(a.ctx, a.CustomerUUID, uUUID)
}

// UpgradeVMImage upgrades the VM image of the universe
func (a *AuthAPIClient) UpgradeVMImage(
	uUUID string,
) ybaclient.UniverseUpgradesManagementApiApiUpgradeVMImageRequest {
	return a.APIClient.UniverseUpgradesManagementApi.UpgradeVMImage(a.ctx, a.CustomerUUID, uUUID)
}

// UpgradeTLS upgrades the TLS settings of the universe
func (a *AuthAPIClient) UpgradeTLS(
	uUUID string,
) ybaclient.UniverseUpgradesManagementApiApiUpgradeTlsRequest {
	return a.APIClient.UniverseUpgradesManagementApi.UpgradeTls(a.ctx, a.CustomerUUID, uUUID)
}

// UpgradeCerts upgrades the TLS certs of the universe
func (a *AuthAPIClient) UpgradeCerts(
	uUUID string,
) ybaclient.UniverseUpgradesManagementApiApiUpgradeCertsRequest {
	return a.APIClient.UniverseUpgradesManagementApi.UpgradeCerts(a.ctx, a.CustomerUUID, uUUID)
}

// RestartUniverse for restart operation
func (a *AuthAPIClient) RestartUniverse(
	uUUID string,
) ybaclient.UniverseUpgradesManagementApiApiRestartUniverseRequest {
	return a.APIClient.UniverseUpgradesManagementApi.RestartUniverse(a.ctx, a.CustomerUUID, uUUID)
}

// SetUniverseKey to change universe EAR settings
func (a *AuthAPIClient) SetUniverseKey(
	uUUID string,
) ybaclient.UniverseManagementApiApiSetUniverseKeyRequest {

	return a.APIClient.UniverseManagementApi.SetUniverseKey(a.ctx, a.CustomerUUID, uUUID)
}

// PauseUniverse for pausing the universe
func (a *AuthAPIClient) PauseUniverse(
	uUUID string,
) ybaclient.UniverseManagementApiApiPauseUniverseRequest {
	return a.APIClient.UniverseManagementApi.PauseUniverse(a.ctx, a.CustomerUUID, uUUID)
}

// ResumeUniverse for resuming the universe
func (a *AuthAPIClient) ResumeUniverse(
	uUUID string,
) ybaclient.UniverseManagementApiApiResumeUniverseRequest {
	return a.APIClient.UniverseManagementApi.ResumeUniverse(a.ctx, a.CustomerUUID, uUUID)
}

// ResizeNode for resizing volumes of primary cluster nodes
func (a *AuthAPIClient) ResizeNode(
	uUUID string,
) ybaclient.UniverseUpgradesManagementApiApiResizeNodeRequest {
	return a.APIClient.UniverseUpgradesManagementApi.ResizeNode(a.ctx, a.CustomerUUID, uUUID)
}

// ConfigureYSQL for YSQL configuration
func (a *AuthAPIClient) ConfigureYSQL(
	uUUID string,
) ybaclient.UniverseDatabaseManagementApiApiConfigureYSQLRequest {
	return a.APIClient.UniverseDatabaseManagementApi.ConfigureYSQL(a.ctx, a.CustomerUUID, uUUID)
}

// ConfigureYCQL for YCQL configuration
func (a *AuthAPIClient) ConfigureYCQL(
	uUUID string,
) ybaclient.UniverseDatabaseManagementApiApiConfigureYCQLRequest {
	return a.APIClient.UniverseDatabaseManagementApi.ConfigureYCQL(a.ctx, a.CustomerUUID, uUUID)
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
