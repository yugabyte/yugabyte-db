/*
 * Copyright (c) YugaByte, Inc.
 */

package client

import (
	"fmt"
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

// RetryTask triggers retry universe/provider API
func (a *AuthAPIClient) RetryTask(tUUID string) (
	ybaclient.CustomerTasksApiApiRetryTaskRequest) {
	return a.APIClient.CustomerTasksApi.RetryTask(a.ctx, a.CustomerUUID, tUUID)
}

// AbortTask triggers abort task API
func (a *AuthAPIClient) AbortTask(tUUID string) (
	ybaclient.CustomerTasksApiApiAbortTaskRequest) {
	return a.APIClient.CustomerTasksApi.AbortTask(a.ctx, a.CustomerUUID, tUUID)
}

// TasksList triggers abort task API
func (a *AuthAPIClient) TasksList() (
	ybaclient.CustomerTasksApiApiTasksListRequest) {
	return a.APIClient.CustomerTasksApi.TasksList(a.ctx, a.CustomerUUID)
}

func (a *AuthAPIClient) failureSubTaskListYBAVersionCheck() (
	bool, string, error) {
	allowedVersions := YBAMinimumVersion{
		Stable: util.YBAAllowFailureSubTaskListMinVersion, 
		Preview: util.YBAAllowFailureSubTaskListMinVersion,
	}
	allowed, version, err := a.CheckValidYBAVersion(allowedVersions)
	if err != nil {
		return false, "", err
	}
	return allowed, version, err
}

// WaitForTask waits for state changes for a YugabyteDB Anywhere task
func (a *AuthAPIClient) WaitForTask(taskUUID, message string) error {
	if strings.ToLower(os.Getenv("YBA_CI")) == "true" {
		return a.WaitForTaskCI(taskUUID, message)
	}
	return a.WaitForTaskProgressBar(taskUUID, message)
}

// WaitForTaskCI waits for State change for a YugabyteDB Anywhere task for CI
func (a *AuthAPIClient) WaitForTaskCI(taskUUID, message string) error {
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
				errMessage := util.ErrorFromHTTPResponse(response, err, "Wait For Task",
					"Get Task Status")
				return errMessage
			}

			currentStatus = r["status"].(string)
			taskProgressString := fmt.Sprintf("Task \"%s\" completion percentage: %.0f%%",
				r["title"].(string),
				r["percent"].(float64))
			output := fmt.Sprintf("%s: %s", message, currentStatus)

			logrus.Infoln(taskProgressString + "\n")

			subtasksDetailsList := r["details"].(map[string]interface{})["taskDetails"].([]interface{})
			var subtasksStatus string
			for _, task := range subtasksDetailsList {
				taskMap := task.(map[string]interface{})
				subtasksStatus = fmt.Sprintf("%sTitle: \"%s\", Status: \"%s\"; \n",
					subtasksStatus, taskMap["title"].(string), taskMap["state"].(string))
			}
			if subtasksStatus != "" {
				logrus.Debugln(fmt.Sprintf("Subtasks: %s\n", subtasksStatus))
			}

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

				logrus.Info(
					fmt.Sprintf(
						"\nOperation failed. Retry operation with \"%s\" command\n",
						formatter.Colorize(
							fmt.Sprintf("yba task retry --task-uuid %s", taskUUID),
							formatter.BlueColor,
						),
					))

				if subtasksFailure != "" {
					logrus.Fatalf(
						formatter.Colorize(
							"Operation failed with state: "+currentStatus+", error: "+
								subtasksFailure+"\n", formatter.RedColor))
				}
				logrus.Fatalf(
					formatter.Colorize(
						"Operation failed with state: "+currentStatus+"\n", formatter.RedColor))
			}

			if previousStatus != currentStatus {
				logrus.Info(output + "\n")
			}

		}
	}

}

// WaitForTaskProgressBar waits for State change for a YugabyteDB Anywhere task
func (a *AuthAPIClient) WaitForTaskProgressBar(taskUUID, message string) error {
	currentStatus := util.UnknownTaskStatus
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

			return fmt.Errorf("wait timeout, operation could still be on-going")
		case <-a.ctx.Done():

			s.Stop()

			return fmt.Errorf("receive interrupt signal, operation could still be on-going")
		case <-checkEveryInSec.C:
			r, response, err := a.GetCustomerTaskStatus(taskUUID).Execute()
			if err != nil {
				errMessage := util.ErrorFromHTTPResponse(response, err, "Wait For Task",
					"Get Task Status")
				return errMessage
			}

			currentStatus = r["status"].(string)
			taskProgressString := fmt.Sprintf("Task \"%s\" completion percentage: %.0f%%",
				r["title"].(string),
				r["percent"].(float64))
			output = fmt.Sprintf("%s: %s", message, currentStatus)

			output = fmt.Sprintf(" %s [%s]",
				output,
				taskProgressString)

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

				logrus.Info(
					fmt.Sprintf(
						"\nOperation failed. Retry operation with \"%s\" command\n",
						formatter.Colorize(
							fmt.Sprintf("yba task retry --task-uuid %s", taskUUID),
							formatter.BlueColor,
						),
					))

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

			s.Suffix = output

		}
	}

}
