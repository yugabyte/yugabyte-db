/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"mime/multipart"
	"net/http"
	"os"
	"path/filepath"

	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	ybav2client "github.com/yugabyte/platform-go-client/v2"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// ListUniverses fetches list of universes associated with the customer
func (a *AuthAPIClient) ListUniverses() ybaclient.UniverseManagementAPIListUniversesRequest {
	return a.APIClient.UniverseManagementAPI.ListUniverses(a.ctx, a.CustomerUUID)
}

// GetUniverse fetches of universe associated with the universeUUID
func (a *AuthAPIClient) GetUniverse(
	uUUID string,
) ybaclient.UniverseManagementAPIGetUniverseRequest {
	return a.APIClient.UniverseManagementAPI.GetUniverse(a.ctx, a.CustomerUUID, uUUID)
}

// DeleteUniverse deletes universe associated with the universeUUID
func (a *AuthAPIClient) DeleteUniverse(
	uUUID string,
) ybaclient.UniverseManagementAPIDeleteUniverseRequest {
	return a.APIClient.UniverseManagementAPI.DeleteUniverse(a.ctx, a.CustomerUUID, uUUID)
}

// CreateAllClusters creates a universe with a minimum of 1 cluster
func (a *AuthAPIClient) CreateAllClusters() ybaclient.UniverseClusterMutationsAPICreateAllClustersRequest {
	return a.APIClient.UniverseClusterMutationsAPI.CreateAllClusters(a.ctx, a.CustomerUUID)
}

// DeleteReadonlyCluster to remove read replica cluster
func (a *AuthAPIClient) DeleteReadonlyCluster(
	uUUID, clusterUUID string,
) ybaclient.UniverseClusterMutationsAPIDeleteReadonlyClusterRequest {
	return a.APIClient.UniverseClusterMutationsAPI.DeleteReadonlyCluster(
		a.ctx, a.CustomerUUID, uUUID, clusterUUID)
}

// CreateReadOnlyCluster to create a read replica cluster
func (a *AuthAPIClient) CreateReadOnlyCluster(
	uUUID string,
) ybaclient.UniverseClusterMutationsAPICreateReadOnlyClusterRequest {
	return a.APIClient.UniverseClusterMutationsAPI.CreateReadOnlyCluster(
		a.ctx, a.CustomerUUID, uUUID)
}

// UpgradeSoftware upgrades the universe YugabyteDB version
func (a *AuthAPIClient) UpgradeSoftware(
	uUUID string,
) ybaclient.UniverseUpgradesManagementAPIUpgradeSoftwareRequest {
	return a.APIClient.UniverseUpgradesManagementAPI.UpgradeSoftware(a.ctx, a.CustomerUUID, uUUID)
}

// UpdatePrimaryCluster to edit primary cluster components
func (a *AuthAPIClient) UpdatePrimaryCluster(
	uUUID string,
) ybaclient.UniverseClusterMutationsAPIUpdatePrimaryClusterRequest {
	return a.APIClient.UniverseClusterMutationsAPI.UpdatePrimaryCluster(
		a.ctx, a.CustomerUUID, uUUID)
}

// UpdateReadOnlyCluster to edit read replica cluster components
func (a *AuthAPIClient) UpdateReadOnlyCluster(
	uUUID string,
) ybaclient.UniverseClusterMutationsAPIUpdateReadOnlyClusterRequest {
	return a.APIClient.UniverseClusterMutationsAPI.UpdateReadOnlyCluster(
		a.ctx, a.CustomerUUID, uUUID)
}

// UpgradeGFlags upgrades the universe gflags
func (a *AuthAPIClient) UpgradeGFlags(
	uUUID string,
) ybaclient.UniverseUpgradesManagementAPIUpgradeGFlagsRequest {
	return a.APIClient.UniverseUpgradesManagementAPI.UpgradeGFlags(a.ctx, a.CustomerUUID, uUUID)
}

// UpgradeVMImage upgrades the VM image of the universe
func (a *AuthAPIClient) UpgradeVMImage(
	uUUID string,
) ybaclient.UniverseUpgradesManagementAPIUpgradeVMImageRequest {
	return a.APIClient.UniverseUpgradesManagementAPI.UpgradeVMImage(a.ctx, a.CustomerUUID, uUUID)
}

// UpgradeTLS upgrades the TLS settings of the universe
func (a *AuthAPIClient) UpgradeTLS(
	uUUID string,
) ybaclient.UniverseUpgradesManagementAPIUpgradeTlsRequest {
	return a.APIClient.UniverseUpgradesManagementAPI.UpgradeTls(a.ctx, a.CustomerUUID, uUUID)
}

// UpgradeCerts upgrades the TLS certs of the universe
func (a *AuthAPIClient) UpgradeCerts(
	uUUID string,
) ybaclient.UniverseUpgradesManagementAPIUpgradeCertsRequest {
	return a.APIClient.UniverseUpgradesManagementAPI.UpgradeCerts(a.ctx, a.CustomerUUID, uUUID)
}

// RestartUniverse for restart operation
func (a *AuthAPIClient) RestartUniverse(
	uUUID string,
) ybaclient.UniverseUpgradesManagementAPIRestartUniverseRequest {
	return a.APIClient.UniverseUpgradesManagementAPI.RestartUniverse(a.ctx, a.CustomerUUID, uUUID)
}

// SetUniverseKey to change universe EAR settings
func (a *AuthAPIClient) SetUniverseKey(
	uUUID string,
) ybaclient.UniverseManagementAPISetUniverseKeyRequest {

	return a.APIClient.UniverseManagementAPI.SetUniverseKey(a.ctx, a.CustomerUUID, uUUID)
}

// PauseUniverse for pausing the universe
func (a *AuthAPIClient) PauseUniverse(
	uUUID string,
) ybaclient.UniverseManagementAPIPauseUniverseRequest {
	return a.APIClient.UniverseManagementAPI.PauseUniverse(a.ctx, a.CustomerUUID, uUUID)
}

// ResumeUniverse for resuming the universe
func (a *AuthAPIClient) ResumeUniverse(
	uUUID string,
) ybaclient.UniverseManagementAPIResumeUniverseRequest {
	return a.APIClient.UniverseManagementAPI.ResumeUniverse(a.ctx, a.CustomerUUID, uUUID)
}

// ResizeNode for resizing volumes of primary cluster nodes
func (a *AuthAPIClient) ResizeNode(
	uUUID string,
) ybaclient.UniverseUpgradesManagementAPIResizeNodeRequest {
	return a.APIClient.UniverseUpgradesManagementAPI.ResizeNode(a.ctx, a.CustomerUUID, uUUID)
}

// ConfigureYSQL for YSQL configuration
func (a *AuthAPIClient) ConfigureYSQL(
	uUUID string,
) ybaclient.UniverseDatabaseManagementAPIConfigureYSQLRequest {
	return a.APIClient.UniverseDatabaseManagementAPI.ConfigureYSQL(a.ctx, a.CustomerUUID, uUUID)
}

// ConfigureYCQL for YCQL configuration
func (a *AuthAPIClient) ConfigureYCQL(
	uUUID string,
) ybaclient.UniverseDatabaseManagementAPIConfigureYCQLRequest {
	return a.APIClient.UniverseDatabaseManagementAPI.ConfigureYCQL(a.ctx, a.CustomerUUID, uUUID)
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

// DetachUniverse detaches the universe from source platform.
// It extracts universe metadata and locks universe.
func (a *AuthAPIClient) DetachUniverse(
	uUUID string,
) ybav2client.UniverseAPIDetachUniverseRequest {
	return a.APIv2Client.UniverseAPI.DetachUniverse(a.ctx, a.CustomerUUID, uUUID)
}

// DeleteAttachDetachMetadata deletes the universe related metadata from
// the source platform. Resources shared by universes like kms configs,
// providers, etc. and associated backups are not deleted.
func (a *AuthAPIClient) DeleteAttachDetachMetadata(
	uUUID string,
) ybav2client.UniverseAPIDeleteAttachDetachMetadataRequest {
	return a.APIv2Client.UniverseAPI.DeleteAttachDetachMetadata(a.ctx, a.CustomerUUID, uUUID)
}

// AttachUniverse attaches the universe into the destination platform.
// Metadata needed by the universe to exist on the destination platform
// is imported.
func (a *AuthAPIClient) AttachUniverse(
	uUUID string,
) ybav2client.UniverseAPIAttachUniverseRequest {
	return a.APIv2Client.UniverseAPI.AttachUniverse(a.ctx, a.CustomerUUID, uUUID)
}

// AttachUniverseRest uses REST API to call attach universe functionality
func (a *AuthAPIClient) AttachUniverseRest(
	sourceUniverseUUID, filePath string,
) error {
	token := viper.GetString("apiToken")
	errorTag := fmt.Errorf("Universe, Operation: Attach")

	file, err := os.Open(filePath)
	if err != nil {
		return fmt.Errorf("%w: Error opening spec file for attach universe %s",
			errorTag,
			err.Error())
	}
	defer file.Close()

	// Create a buffer and a multipart writer
	bodyBuffer := &bytes.Buffer{}
	writer := multipart.NewWriter(bodyBuffer)

	// Add the file field to the form
	part, err := writer.CreateFormFile("downloaded_spec_file", filepath.Base(filePath))
	if err != nil {
		return fmt.Errorf("%w: Error creating form file for attach universe %s",
			errorTag,
			err.Error())
	}

	// Copy the file content into the form field
	_, err = io.Copy(part, file)
	if err != nil {
		return fmt.Errorf("%w: Error copying file content for attach universe %s",
			errorTag,
			err.Error())
	}

	// Close the writer to finalize the form data
	err = writer.Close()
	if err != nil {
		return fmt.Errorf("%w: Error closing writer for attach universe %s",
			errorTag,
			err.Error())
	}

	var req *http.Request

	req, err = http.NewRequest(
		http.MethodPost,
		fmt.Sprintf("%s://%s/api/v2/customers/%s/universes/%s/attach",
			a.RestClient.Scheme, a.RestClient.Host, a.CustomerUUID, sourceUniverseUUID),
		bodyBuffer,
	)

	if err != nil {
		return fmt.Errorf("%w: %s", errorTag, err.Error())
	}

	req.Header.Set("Content-Type", writer.FormDataContentType())
	req.Header.Set("X-AUTH-YW-API-TOKEN", token)

	r, err := a.RestClient.Client.Do(req)
	if err != nil {
		return fmt.Errorf("%w: Error occurred during POST call for attach universe %s",
			errorTag,
			err.Error())
	}
	defer r.Body.Close()

	var body []byte
	body, err = io.ReadAll(r.Body)
	if err != nil {
		return fmt.Errorf("%w: Error reading attach universe response body %s",
			errorTag,
			err.Error())
	}

	if r.StatusCode < 200 || r.StatusCode >= 300 {
		responseBodyError := util.YbaStructuredError{}
		if err = json.Unmarshal(body, &responseBodyError); err != nil {
			return fmt.Errorf("%w: Failed unmarshalling attach universe error response body %s",
				errorTag,
				err.Error())
		}

		errorMessage := util.ErrorFromResponseBody(responseBodyError)
		return fmt.Errorf("%w: Error attaching universe: %s", errorTag, errorMessage)
	}

	return nil
}

// NewAttachDetachYBAVersionCheck checks if the new API request body can be used for the
// Attach/detach APIs
func (a *AuthAPIClient) NewAttachDetachYBAVersionCheck() (bool, string, error) {
	allowedVersions := YBAMinimumVersion{
		Stable:  util.YBAAllowNewAttachDetachMinStableVersion,
		Preview: util.YBAAllowNewAttachDetachMinPreviewVersion,
	}
	allowed, version, err := a.CheckValidYBAVersion(allowedVersions)
	if err != nil {
		return false, "", err
	}
	return allowed, version, err
}
