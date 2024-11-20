/*
 * Copyright (c) YugaByte, Inc.
 */

package client

import (
	"fmt"
	"net/http"
	"os"
	"strings"
	"time"

	"github.com/briandowns/spinner"
	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"golang.org/x/exp/slices"
)

// Refresh the releases page to get the latest YugabyteDB releases
func (a *AuthAPIClient) Refresh() (
	ybaclient.ReleaseManagementApiApiRefreshRequest,
) {
	return a.APIClient.ReleaseManagementApi.Refresh(a.ctx, a.CustomerUUID)
}

// GetListOfReleases API to fetch list of releases
func (a *AuthAPIClient) GetListOfReleases(includeMetadata bool) (
	ybaclient.ReleaseManagementApiApiGetListOfReleasesRequest,
) {
	return a.APIClient.ReleaseManagementApi.
		GetListOfReleases(a.ctx, a.CustomerUUID).
		IncludeMetadata(includeMetadata)
}

// ListNewReleases API to fetch list of new releases
func (a *AuthAPIClient) ListNewReleases() (
	ybaclient.NewReleaseManagementApiApiListNewReleasesRequest,
) {
	return a.APIClient.NewReleaseManagementApi.ListNewReleases(a.ctx, a.CustomerUUID)
}

// GetNewRelease API to fetch list of new releases
func (a *AuthAPIClient) GetNewRelease(rUUID string) (
	ybaclient.NewReleaseManagementApiApiGetNewReleaseRequest,
) {
	return a.APIClient.NewReleaseManagementApi.GetNewRelease(a.ctx, a.CustomerUUID, rUUID)
}

// CreateNewRelease API to create new release
func (a *AuthAPIClient) CreateNewRelease() (
	ybaclient.NewReleaseManagementApiApiCreateNewReleaseRequest,
) {
	return a.APIClient.NewReleaseManagementApi.CreateNewRelease(a.ctx, a.CustomerUUID)
}

// DeleteNewRelease API to delete new release
func (a *AuthAPIClient) DeleteNewRelease(rUUID string) (
	ybaclient.NewReleaseManagementApiApiDeleteNewReleaseRequest,
) {
	return a.APIClient.NewReleaseManagementApi.DeleteNewRelease(a.ctx, a.CustomerUUID, rUUID)
}

// UpdateNewRelease API to update new release
func (a *AuthAPIClient) UpdateNewRelease(rUUID string) (
	ybaclient.NewReleaseManagementApiApiUpdateNewReleaseRequest,
) {
	return a.APIClient.NewReleaseManagementApi.UpdateNewRelease(a.ctx, a.CustomerUUID, rUUID)
}

// UploadRelease API to upload URL
func (a *AuthAPIClient) UploadRelease() (
	ybaclient.UploadReleasePackagesApiApiUploadReleaseRequest,
) {
	return a.APIClient.UploadReleasePackagesApi.UploadRelease(a.ctx, a.CustomerUUID)
}

// GetUploadRelease API to get URL
func (a *AuthAPIClient) GetUploadRelease(fileUUID string) (
	ybaclient.UploadReleasePackagesApiApiGetUploadReleaseRequest,
) {
	return a.APIClient.UploadReleasePackagesApi.GetUploadRelease(a.ctx, a.CustomerUUID, fileUUID)
}

// ExtractMetadata API to extract metadata from tarball
func (a *AuthAPIClient) ExtractMetadata() (
	ybaclient.ExtractMetadataFromRemoteTarballApiApiExtractMetadataRequest,
) {
	return a.APIClient.ExtractMetadataFromRemoteTarballApi.ExtractMetadata(a.ctx, a.CustomerUUID)
}

// GetExtractMetadata API to get extract metadata
func (a *AuthAPIClient) GetExtractMetadata(fileUUID string) (
	ybaclient.ExtractMetadataFromRemoteTarballApiApiExtractMetadata_0Request,
) {
	return a.APIClient.ExtractMetadataFromRemoteTarballApi.ExtractMetadata_1(
		a.ctx,
		a.CustomerUUID,
		fileUUID)
}

// NewReleaseYBAVersionCheck checks if the new release management API can be used
func (a *AuthAPIClient) NewReleaseYBAVersionCheck() (bool, string, error) {
	allowedVersions := YBAMinimumVersion{
		Stable:  util.YBAAllowNewReleaseMinStableVersion,
		Preview: util.YBAAllowNewReleaseMinPreviewVersion,
	}
	allowed, version, err := a.CheckValidYBAVersion(allowedVersions)
	if err != nil {
		return false, "", err
	}
	return allowed, version, err
}

// WaitForExtractMetadata waits for state changes for a YugabyteDB Anywhere task
func (a *AuthAPIClient) WaitForExtractMetadata(resourceUUID, message, operation string) (
	ybaclient.ResponseExtractMetadata,
	error,
) {
	if strings.ToLower(os.Getenv("YBA_CI")) == "true" {
		return a.WaitForExtractMetadataCI(resourceUUID, message, operation)
	}
	return a.WaitForExtractMetadataProgressBar(resourceUUID, message, operation)
}

// WaitForExtractMetadataCI waits for State change for a YugabyteDB Anywhere task for CI
func (a *AuthAPIClient) WaitForExtractMetadataCI(resourceUUID, message, operation string) (
	ybaclient.ResponseExtractMetadata,
	error,
) {
	currentStatus := util.RunningReleaseResponseState
	previousStatus := util.RunningReleaseResponseState

	timeout := time.After(viper.GetDuration("timeout"))
	checkEveryInSec := time.NewTicker(2 * time.Second)
	for {
		select {
		case <-timeout:

			return ybaclient.ResponseExtractMetadata{},
				fmt.Errorf("wait timeout, operation could still be on-going")
		case <-a.ctx.Done():

			return ybaclient.ResponseExtractMetadata{},
				fmt.Errorf("receive interrupt signal, operation could still be on-going")
		case <-checkEveryInSec.C:
			r := ybaclient.ResponseExtractMetadata{}
			var response *http.Response
			var err error
			if strings.Compare(operation, "url") == 0 {
				r, response, err = a.GetExtractMetadata(resourceUUID).Execute()
				if err != nil {
					errMessage := util.ErrorFromHTTPResponse(
						response,
						err,
						"Release",
						"URL Extract Metadata")
					logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
				}
			} else {
				r, response, err = a.GetUploadRelease(resourceUUID).Execute()
				if err != nil {
					errMessage := util.ErrorFromHTTPResponse(
						response,
						err,
						"Release",
						"File Extract Metadata")
					logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
				}
			}
			currentStatus = r.GetStatus()
			taskProgressString := fmt.Sprintf("Extracting metadata for resource %s", resourceUUID)
			output := fmt.Sprintf("%s: %s", message, currentStatus)

			logrus.Infoln(taskProgressString + "\n")

			if slices.Contains(util.CompletedReleaseReponseStates(), currentStatus) {
				if !slices.Contains(util.ErrorReleaseResponseStates(), currentStatus) {
					return r, nil
				}
				logrus.Info(
					formatter.Colorize(
						"Failed to extract metadata for resource "+resourceUUID+". "+
							"Please provide the required information in the "+
							"\"yba yb-db-version create\" command\n",
						formatter.RedColor,
					),
				)
				return r, nil
			}

			if previousStatus != currentStatus {
				logrus.Info(output + "\n")
			}

		}
	}

}

// WaitForExtractMetadataProgressBar waits for State change for a YugabyteDB Anywhere task
func (a *AuthAPIClient) WaitForExtractMetadataProgressBar(resourceUUID, message, operation string) (
	ybaclient.ResponseExtractMetadata,
	error,
) {
	currentStatus := util.RunningReleaseResponseState
	output := fmt.Sprintf(" %s: %s", message, currentStatus)

	s := spinner.New(spinner.CharSets[36], 300*time.Millisecond)
	s.Color(formatter.GreenColor)
	s.Start()
	s.Suffix = " " + output
	s.FinalMSG = ""
	defer s.Stop()

	timeout := time.After(viper.GetDuration("timeout"))
	checkEveryInSec := time.NewTicker(2 * time.Second)
	for {
		select {
		case <-timeout:

			s.Stop()

			return ybaclient.ResponseExtractMetadata{},
				fmt.Errorf("wait timeout, operation could still be on-going")
		case <-a.ctx.Done():

			s.Stop()

			return ybaclient.ResponseExtractMetadata{},
				fmt.Errorf("receive interrupt signal, operation could still be on-going")
		case <-checkEveryInSec.C:
			r := ybaclient.ResponseExtractMetadata{}
			var response *http.Response
			var err error
			if strings.Compare(operation, "url") == 0 {
				r, response, err = a.GetExtractMetadata(resourceUUID).Execute()
				if err != nil {
					errMessage := util.ErrorFromHTTPResponse(
						response,
						err,
						"Release",
						"URL Extract Metadata")
					logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
				}
			} else {
				r, response, err = a.GetUploadRelease(resourceUUID).Execute()
				if err != nil {
					errMessage := util.ErrorFromHTTPResponse(
						response,
						err,
						"Release",
						"File Extract Metadata")
					logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
				}
			}

			currentStatus = r.GetStatus()
			taskProgressString := fmt.Sprintf("Extracting metadata for resource %s", resourceUUID)
			output = fmt.Sprintf("%s: %s", message, currentStatus)

			output = fmt.Sprintf(" %s [%s]",
				output,
				taskProgressString)

			if slices.Contains(util.CompletedReleaseReponseStates(), currentStatus) {
				if !slices.Contains(util.ErrorReleaseResponseStates(), currentStatus) {
					return r, nil
				}
				logrus.Info(
					formatter.Colorize(
						"Failed to extract metadata for resource "+resourceUUID+". "+
							"Please provide the required information in the "+
							"\"yba yb-db-version create\" command\n",
						formatter.RedColor,
					),
				)
				return r, nil
			}

			s.Suffix = output

		}
	}

}
