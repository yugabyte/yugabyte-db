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
func (a *AuthAPIClient) Refresh() ybaclient.ReleaseManagementAPIRefreshRequest {
	return a.APIClient.ReleaseManagementAPI.Refresh(a.ctx, a.CustomerUUID)
}

// GetListOfReleases API to fetch list of releases
func (a *AuthAPIClient) GetListOfReleases(
	includeMetadata bool,
) ybaclient.ReleaseManagementAPIGetListOfReleasesRequest {
	return a.APIClient.ReleaseManagementAPI.
		GetListOfReleases(a.ctx, a.CustomerUUID).
		IncludeMetadata(includeMetadata)
}

// ListNewReleases API to fetch list of new releases
func (a *AuthAPIClient) ListNewReleases() ybaclient.NewReleaseManagementAPIListNewReleasesRequest {
	return a.APIClient.NewReleaseManagementAPI.ListNewReleases(a.ctx, a.CustomerUUID)
}

// GetNewRelease API to fetch list of new releases
func (a *AuthAPIClient) GetNewRelease(
	rUUID string,
) ybaclient.NewReleaseManagementAPIGetNewReleaseRequest {
	return a.APIClient.NewReleaseManagementAPI.GetNewRelease(a.ctx, a.CustomerUUID, rUUID)
}

// CreateNewRelease API to create new release
func (a *AuthAPIClient) CreateNewRelease() ybaclient.NewReleaseManagementAPICreateNewReleaseRequest {
	return a.APIClient.NewReleaseManagementAPI.CreateNewRelease(a.ctx, a.CustomerUUID)
}

// DeleteNewRelease API to delete new release
func (a *AuthAPIClient) DeleteNewRelease(
	rUUID string,
) ybaclient.NewReleaseManagementAPIDeleteNewReleaseRequest {
	return a.APIClient.NewReleaseManagementAPI.DeleteNewRelease(a.ctx, a.CustomerUUID, rUUID)
}

// UpdateNewRelease API to update new release
func (a *AuthAPIClient) UpdateNewRelease(
	rUUID string,
) ybaclient.NewReleaseManagementAPIUpdateNewReleaseRequest {
	return a.APIClient.NewReleaseManagementAPI.UpdateNewRelease(a.ctx, a.CustomerUUID, rUUID)
}

// UploadRelease API to upload URL
func (a *AuthAPIClient) UploadRelease() ybaclient.UploadReleasePackagesAPIUploadReleaseRequest {
	return a.APIClient.UploadReleasePackagesAPI.UploadRelease(a.ctx, a.CustomerUUID)
}

// GetUploadRelease API to get URL
func (a *AuthAPIClient) GetUploadRelease(
	fileUUID string,
) ybaclient.UploadReleasePackagesAPIGetUploadReleaseRequest {
	return a.APIClient.UploadReleasePackagesAPI.GetUploadRelease(a.ctx, a.CustomerUUID, fileUUID)
}

// ExtractMetadata API to extract metadata from tarball
func (a *AuthAPIClient) ExtractMetadata() ybaclient.ExtractMetadataFromRemoteTarballAPIExtractMetadataRequest {
	return a.APIClient.ExtractMetadataFromRemoteTarballAPI.ExtractMetadata(a.ctx, a.CustomerUUID)
}

// GetExtractMetadata API to get extract metadata
func (a *AuthAPIClient) GetExtractMetadata(
	fileUUID string,
) ybaclient.ExtractMetadataFromRemoteTarballAPIExtractMetadata_0Request {
	return a.APIClient.ExtractMetadataFromRemoteTarballAPI.ExtractMetadata_1(
		a.ctx,
		a.CustomerUUID,
		fileUUID)
}

// UploadReleaseRest uses REST API to call list schedule functionality
func (a *AuthAPIClient) UploadReleaseRest(
	filePath string,
) (
	ybaclient.YBPCreateSuccess, error,
) {
	token := viper.GetString("apiToken")
	errorTag := fmt.Errorf("Release, Operation: Upload")

	file, err := os.Open(filePath)
	if err != nil {
		return ybaclient.YBPCreateSuccess{},
			fmt.Errorf("%w: Error opening file for upload YugabyteDB version %s",
				errorTag,
				err.Error())
	}
	defer file.Close()

	// Create a buffer and a multipart writer
	bodyBuffer := &bytes.Buffer{}
	writer := multipart.NewWriter(bodyBuffer)

	// Add the file field to the form
	part, err := writer.CreateFormFile("file", file.Name())
	if err != nil {
		return ybaclient.YBPCreateSuccess{},
			fmt.Errorf("%w: Error creating form file for upload YugabyteDB version %s",
				errorTag,
				err.Error())
	}

	// Copy the file content into the form field
	_, err = io.Copy(part, file)
	if err != nil {
		return ybaclient.YBPCreateSuccess{},
			fmt.Errorf("%w: Error copying file content for upload YugabyteDB version %s",
				errorTag,
				err.Error())
	}

	// Close the writer to finalize the form data
	err = writer.Close()
	if err != nil {
		return ybaclient.YBPCreateSuccess{},
			fmt.Errorf("%w: Error Error closing writer for upload YugabyteDB version %s",
				errorTag,
				err.Error())
	}

	var req *http.Request

	req, err = http.NewRequest(
		http.MethodPost,
		fmt.Sprintf("%s://%s/api/v1/customers/%s/ybdb_release/upload",
			a.RestClient.Scheme, a.RestClient.Host, a.CustomerUUID),
		bodyBuffer,
	)

	if err != nil {
		return ybaclient.YBPCreateSuccess{},
			fmt.Errorf("%w: %s", errorTag, err.Error())
	}

	req.Header.Set("Content-Type", writer.FormDataContentType())
	req.Header.Set("X-AUTH-YW-API-TOKEN", token)

	r, err := a.RestClient.Client.Do(req)
	if err != nil {
		return ybaclient.YBPCreateSuccess{},
			fmt.Errorf("%w: Error occured during POST call for upload YugabyteDB version %s",
				errorTag,
				err.Error())
	}

	var body []byte
	body, err = io.ReadAll(r.Body)
	if err != nil {
		return ybaclient.YBPCreateSuccess{},
			fmt.Errorf("%w: Error reading upload YugabyteDB version response body %s",
				errorTag,
				err.Error())
	}

	responseBody := ybaclient.YBPCreateSuccess{}
	if err = json.Unmarshal(body, &responseBody); err != nil {
		return ybaclient.YBPCreateSuccess{},
			fmt.Errorf("%w: Failed unmarshalling upload YugabyteDB version response body %s",
				errorTag,
				err.Error())
	}

	if responseBody.ResourceUUID != nil {
		return responseBody, nil
	}

	responseBodyError := util.YbaStructuredError{}
	if err = json.Unmarshal(body, &responseBodyError); err != nil {
		return ybaclient.YBPCreateSuccess{},
			fmt.Errorf("%w: Failed unmarshalling upload YugabyteDB version error response body %s",
				errorTag,
				err.Error())
	}

	errorMessage := util.ErrorFromResponseBody(responseBodyError)
	return ybaclient.YBPCreateSuccess{},
		fmt.Errorf("%w: Error fetching YugabyteDB version: %s", errorTag, errorMessage)

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
			r := &ybaclient.ResponseExtractMetadata{}
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
					return *r, nil
				}
				logrus.Info(
					formatter.Colorize(
						"Failed to extract metadata for resource "+resourceUUID+". "+
							"Please provide the required information in the "+
							"\"yba yb-db-version create\" command\n",
						formatter.RedColor,
					),
				)
				return *r, nil
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
			r := &ybaclient.ResponseExtractMetadata{}
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
					return *r, nil
				}
				logrus.Info(
					formatter.Colorize(
						"Failed to extract metadata for resource "+resourceUUID+". "+
							"Please provide the required information in the "+
							"\"yba yb-db-version create\" command\n",
						formatter.RedColor,
					),
				)
				return *r, nil
			}

			s.Suffix = output

		}
	}

}
