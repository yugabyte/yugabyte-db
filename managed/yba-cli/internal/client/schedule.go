/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net/http"

	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// CreateBackupSchedule creates a backup schedule
func (a *AuthAPIClient) CreateBackupSchedule() ybaclient.BackupsAPICreateBackupScheduleAsyncRequest {
	return a.APIClient.BackupsAPI.CreateBackupScheduleAsync(a.ctx, a.CustomerUUID)
}

// ListBackupSchedules associated with the customer
func (a *AuthAPIClient) ListBackupSchedules() ybaclient.ScheduleManagementAPIListSchedulesV2Request {
	return a.APIClient.ScheduleManagementAPI.ListSchedulesV2(a.ctx, a.CustomerUUID)
}

// GetBackupSchedule fetches backups by the universe and task UUID
func (a *AuthAPIClient) GetBackupSchedule(
	scheduleUUID string,
) ybaclient.ScheduleManagementAPIGetScheduleRequest {
	return a.APIClient.ScheduleManagementAPI.GetSchedule(a.ctx, a.CustomerUUID, scheduleUUID)
}

// DeleteBackupSchedule deletes the backup schedule
func (a *AuthAPIClient) DeleteBackupSchedule(
	scheduleUUID string,
) ybaclient.ScheduleManagementAPIDeleteScheduleV2Request {
	return a.APIClient.ScheduleManagementAPI.DeleteScheduleV2(a.ctx, a.CustomerUUID, scheduleUUID)
}

// EditBackupSchedule edits a backup schedule
func (a *AuthAPIClient) EditBackupSchedule(
	scheduleUUID string,
) ybaclient.ScheduleManagementAPIEditBackupScheduleV2Request {
	return a.APIClient.ScheduleManagementAPI.EditBackupScheduleV2(
		a.ctx,
		a.CustomerUUID,
		scheduleUUID)
}

// ListBackupSchedulesRest uses REST API to call list schedule functionality
func (a *AuthAPIClient) ListBackupSchedulesRest(
	reqBody ybaclient.SchedulePagedApiQuery,
	operation string,
) (
	util.SchedulePagedResponse, error,
) {
	token := viper.GetString("apiToken")
	errorTag := fmt.Errorf("Backup Schedules, Operation: %s", operation)

	reqBytes, err := json.Marshal(reqBody)
	if err != nil {
		return util.SchedulePagedResponse{},
			fmt.Errorf("%w: %s", errorTag, err.Error())
	}

	reqBuf := bytes.NewBuffer(reqBytes)

	var req *http.Request

	req, err = http.NewRequest(
		http.MethodPost,
		fmt.Sprintf("%s://%s/api/v1/customers/%s/schedules/page",
			a.RestClient.Scheme, a.RestClient.Host, a.CustomerUUID),
		reqBuf,
	)

	if err != nil {
		return util.SchedulePagedResponse{},
			fmt.Errorf("%w: %s", errorTag, err.Error())
	}

	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("X-AUTH-YW-API-TOKEN", token)

	r, err := a.RestClient.Client.Do(req)
	if err != nil {
		return util.SchedulePagedResponse{},
			fmt.Errorf("%w: Error occured during POST call for list backup schedule %s",
				errorTag,
				err.Error())
	}

	var body []byte
	body, err = io.ReadAll(r.Body)
	if err != nil {
		return util.SchedulePagedResponse{},
			fmt.Errorf("%w: Error reading list backup schedule response body %s",
				errorTag,
				err.Error())
	}

	responseBody := util.SchedulePagedResponse{}
	if err = json.Unmarshal(body, &responseBody); err != nil {
		return util.SchedulePagedResponse{},
			fmt.Errorf("%w: Failed unmarshalling list backup schedule response body %s",
				errorTag,
				err.Error())
	}

	if responseBody.Entities != nil {
		return responseBody, nil
	}

	responseBodyError := util.YbaStructuredError{}
	if err = json.Unmarshal(body, &responseBodyError); err != nil {
		return util.SchedulePagedResponse{},
			fmt.Errorf("%w: Failed unmarshalling list backup schedule error response body %s",
				errorTag,
				err.Error())
	}

	errorMessage := util.ErrorFromResponseBody(responseBodyError)
	return util.SchedulePagedResponse{},
		fmt.Errorf("%w: Error fetching list of schedules: %s", errorTag, errorMessage)

}
