/*
 * Copyright (c) YugaByte, Inc.
 */

package client

import (
	"context"
	"crypto/tls"
	"errors"
	"fmt"
	"net/http"
	"net/url"
	"os"
	"os/signal"
	"strings"
	"time"

	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"golang.org/x/exp/slices"
)

// AuthAPIClient is a auth YBA Client

var cliVersion = "v0.1.0"

// AuthAPIClient contains authenticated api client and customer UUID
type AuthAPIClient struct {
	APIClient    *ybaclient.APIClient
	CustomerUUID string
	ctx          context.Context
}

// SetVersion assigns the version of YBA CLI
func SetVersion(version string) {
	cliVersion = version
}

// GetVersion fetches the version of YBA CLI
func GetVersion() string {
	return cliVersion
}

// NewAuthAPIClient function is returning a new AuthAPIClient Client
func NewAuthAPIClient() (*AuthAPIClient, error) {
	host := viper.GetString("host")
	// If the host is empty, then tell the user to run the auth command.
	// Need to check if current instance has a Yugabyte Anywhere installation.
	if len(host) == 0 {
		logrus.Fatalln(
			formatter.Colorize(
				"No valid Host detected. Run `yba auth` to authenticate with YugabyteDB Anywhere.",
				formatter.RedColor))
	}
	url, err := ParseURL(host)
	if err != nil {
		logrus.Error(err)
		return nil, err
	}

	apiToken := viper.GetString("apiToken")
	// If the api token is empty, then tell the user to run the auth command.
	if len(apiToken) == 0 {
		logrus.Fatalln(
			formatter.Colorize(
				"No valid API token detected. Run `yba auth` to "+
					"authenticate with YugabyteDB Anywhere or run the command with -a flag.",
				formatter.RedColor))
	}

	return NewAuthAPIClientInitialize(url, apiToken)
}

// NewAuthAPIClientInitialize function is returning a new AuthAPIClient Client
func NewAuthAPIClientInitialize(url *url.URL, apiToken string) (*AuthAPIClient, error) {

	cfg := ybaclient.NewConfiguration()
	//Configure the client

	cfg.Host = url.Host
	cfg.Scheme = url.Scheme
	if url.Scheme == "https" {
		cfg.Scheme = "https"
		tr := &http.Transport{TLSClientConfig: &tls.Config{InsecureSkipVerify: true}}
		cfg.HTTPClient = &http.Client{Transport: tr}
	} else {
		cfg.Scheme = "http"
	}

	cfg.DefaultHeader = map[string]string{"X-AUTH-YW-API-TOKEN": apiToken}

	apiClient := ybaclient.NewAPIClient(cfg)

	ctx, _ := signal.NotifyContext(context.Background(), os.Interrupt)

	return &AuthAPIClient{
		apiClient,
		"",
		ctx,
	}, nil
}

// ParseURL returns a URL if string is valid, or returns error
func ParseURL(host string) (*url.URL, error) {
	if strings.HasPrefix(strings.ToLower(host), "http://") {
		logrus.Warnf("You are using insecure api endpoint %s\n", host)
	} else if !strings.HasPrefix(strings.ToLower(host), "https://") {
		host = "https://" + host
	}

	endpoint, err := url.ParseRequestURI(host)
	if err != nil {
		return nil, fmt.Errorf("could not parse YBA url (%s): %w", host, err)
	}
	return endpoint, err
}

// GetAppVersion fetches YugabyteDB Anywhere version
func (a *AuthAPIClient) GetAppVersion() ybaclient.SessionManagementApiApiAppVersionRequest {
	return a.APIClient.SessionManagementApi.AppVersion(a.ctx)
}

// GetSessionInfo fetches YugabyteDB Anywhere session info
func (a *AuthAPIClient) GetSessionInfo() (
	ybaclient.SessionManagementApiApiGetSessionInfoRequest) {
	return a.APIClient.SessionManagementApi.GetSessionInfo(a.ctx)
}

// GetCustomerUUID fetches YugabyteDB Anywhere customer UUID
func (a *AuthAPIClient) GetCustomerUUID() error {
	r, response, err := a.GetSessionInfo().Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(response, err,
			"GetCustomerUUID", "Get Session Info")
		logrus.Errorf("%s", errMessage.Error())
		return errMessage
	}
	if !r.HasCustomerUUID() {
		err := "could not retrieve Customer UUID"
		logrus.Errorf(err)
		return errors.New(err)
	}
	a.CustomerUUID = *r.CustomerUUID
	return nil
}

// GetCustomerTaskStatus fetches the customer task status
func (a *AuthAPIClient) GetCustomerTaskStatus(taskUUID string) (
	ybaclient.CustomerTasksApiApiTaskStatusRequest) {
	return a.APIClient.CustomerTasksApi.TaskStatus(a.ctx, a.CustomerUUID, taskUUID)
}

// ListFailedSubtasks fetches the customer failed task status
func (a *AuthAPIClient) ListFailedSubtasks(taskUUID string) (
	ybaclient.CustomerTasksApiApiListFailedSubtasksRequest) {
	return a.APIClient.CustomerTasksApi.ListFailedSubtasks(a.ctx, a.CustomerUUID, taskUUID)
}

// CheckValidYBAVersion allows operation if version is higher than listed versions
func (a *AuthAPIClient) CheckValidYBAVersion(versions []string) (bool,
	string, error) {

	r, response, err := a.GetAppVersion().Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(response, err,
			"YBA Version", "Get App Version")
		return false, "", errMessage
	}
	currentVersion := r["version"]
	for _, v := range versions {
		check, err := util.CompareYbVersions(currentVersion, v)
		if err != nil {
			return false, "", err
		}
		if check == 0 || check == 1 {
			return true, currentVersion, err
		}
	}
	return false, currentVersion, err
}

// WaitForTask waits for State change for a YugabyteDB Anywhere task
func (a *AuthAPIClient) WaitForTask(taskUUID, message string) error {

	currentStatus := util.UnknownTaskStatus
	previousStatus := util.UnknownTaskStatus
	timeout := time.After(viper.GetDuration("timeout"))
	checkEveryInSec := time.NewTicker(2 * time.Second)
	for {
		select {
		case <-timeout:
			return fmt.Errorf("wait timeout, operation could still be on-going")
		case <-a.ctx.Done():
			return fmt.Errorf("receive interrupt signal, operation could still be on-going")
		case <-checkEveryInSec.C:
			r, response, err := a.GetCustomerTaskStatus(taskUUID).Execute()
			if err != nil {
				errMessage := util.ErrorFromHTTPResponse(response, err, "WaitForTask",
					"Get Task Status")
				return errMessage
			}
			logrus.Infoln(fmt.Sprintf("Task \"%s\" completion percentage: %.0f%%\n", r["title"].(string),
				r["percent"].(float64)))

			subtasksDetailsList := r["details"].(map[string]interface{})["taskDetails"].([]interface{})
			var subtasksStatus string
			for _, task := range subtasksDetailsList {
				taskMap := task.(map[string]interface{})
				subtasksStatus = fmt.Sprintf("%sTitle: \"%s\", Status: \"%s\"; \n",
					subtasksStatus, taskMap["title"].(string), taskMap["state"].(string))
			}
			if subtasksStatus != "" {
				logrus.Debugln(fmt.Sprintf("Subtasks: %s", subtasksStatus))
			}
			currentStatus = r["status"].(string)

			if slices.Contains(util.CompletedStates(), currentStatus) {
				if !slices.Contains(util.ErrorStates(), currentStatus) {
					return nil
				}
				allowed, _, errV := a.failureSubTaskListYBAVersionCheck()
				if errV != nil {
					return errV
				}
				var subtasksFailure string
				if allowed {
					r, response, errR := a.ListFailedSubtasks(taskUUID).Execute()
					if errR != nil {
						errMessage := util.ErrorFromHTTPResponse(response, errR, "ListFailedSubtasks",
							"Get Failed Tasks")
						return errMessage
					}

					for _, f := range r.GetFailedSubTasks() {
						subtasksFailure = fmt.Sprintf("%sSubTaskType: \"%s\", Error: \"%s\"; \n",
							subtasksFailure, f.GetSubTaskType(), f.GetErrorString())
					}
				} else {
					subtasksFailure = fmt.Sprintln("Please refer to the YugabyteDB Anywhere Tasks",
						"for description")
				}
				if subtasksFailure != "" {
					logrus.Fatalf(
						formatter.Colorize(
							"Operation failed with state: "+currentStatus+", error: "+
								subtasksFailure, formatter.RedColor))
				}
				logrus.Fatalf(
					formatter.Colorize(
						"Operation failed with state: "+currentStatus, formatter.RedColor))
			}

			if previousStatus != currentStatus {
				output := fmt.Sprintf("%s: %s", message, currentStatus)
				logrus.Info(output + "\n")
			}
		}
	}

}

func (a *AuthAPIClient) failureSubTaskListYBAVersionCheck() (
	bool, string, error) {
	allowedVersions := []string{util.YBAAllowFailureSubTaskListMinVersion}
	allowed, version, err := a.CheckValidYBAVersion(allowedVersions)
	if err != nil {
		return false, "", err
	}
	if allowed {
		// if the release is 2.19.0.0, block it like YBA < 2.18.1.0 and send generic message
		restrictedVersions := util.YBARestrictFailedSubtasksVersions()
		for _, i := range restrictedVersions {
			allowed, err = util.IsVersionAllowed(version, i)
			if err != nil {
				return false, version, err
			}
		}
	}
	return allowed, version, err
}
