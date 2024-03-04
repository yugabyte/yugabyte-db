/*
 * Copyright (c) YugaByte, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// CreateBackup creates backups associated with the universe
func (a *AuthAPIClient) CreateBackup() ybaclient.BackupsApiApiCreatebackupRequest {
	return a.APIClient.BackupsApi.Createbackup(a.ctx, a.CustomerUUID)
}

// List Backups
func (a *AuthAPIClient) ListBackups() ybaclient.BackupsApiApiListBackupsV2Request {
	return a.APIClient.BackupsApi.ListBackupsV2(a.ctx, a.CustomerUUID)
}

func (a *AuthAPIClient) GetBackupByTasKUUID(universeUUID string, taskUUID string) ybaclient.BackupsApiApiFetchBackupsByTaskUUIDRequest {
	return a.APIClient.BackupsApi.FetchBackupsByTaskUUID(a.ctx, a.CustomerUUID, universeUUID, taskUUID)
}

func (a *AuthAPIClient) EditBackup(backupUUID string) ybaclient.BackupsApiApiEditBackupV2Request {
	return a.APIClient.BackupsApi.EditBackupV2(a.ctx, a.CustomerUUID, backupUUID)
}

func (a *AuthAPIClient) DeleteBackups() ybaclient.BackupsApiApiDeleteBackupsV2Request {
	return a.APIClient.BackupsApi.DeleteBackupsV2(a.ctx, a.CustomerUUID)
}

func (a *AuthAPIClient) ListIncrementalBackups(backupUUID string) ybaclient.BackupsApiApiListIncrementalBackupsRequest {
	return a.APIClient.BackupsApi.ListIncrementalBackups(a.ctx, a.CustomerUUID, backupUUID)
}

func (a *AuthAPIClient) CreateBackupSchedule() ybaclient.BackupsApiApiCreateBackupScheduleAsyncRequest {
	return a.APIClient.BackupsApi.CreateBackupScheduleAsync(a.ctx, a.CustomerUUID)
}

func (a *AuthAPIClient) ListBackupSchedules() ybaclient.ScheduleManagementApiApiListSchedulesV2Request {
	return a.APIClient.ScheduleManagementApi.ListSchedulesV2(a.ctx, a.CustomerUUID)
}

func (a *AuthAPIClient) GetBackupSchedule(scheduleUUID string) ybaclient.ScheduleManagementApiApiGetScheduleRequest {
	return a.APIClient.ScheduleManagementApi.GetSchedule(a.ctx, a.CustomerUUID, scheduleUUID)
}

func (a *AuthAPIClient) DeleteBackupSchedule(scheduleUUID string) ybaclient.ScheduleManagementApiApiDeleteScheduleV2Request {
	return a.APIClient.ScheduleManagementApi.DeleteScheduleV2(a.ctx, a.CustomerUUID, scheduleUUID)
}

func (a *AuthAPIClient) EditBackupSchedule(scheduleUUID string) ybaclient.ScheduleManagementApiApiEditBackupScheduleV2Request {
	return a.APIClient.ScheduleManagementApi.EditBackupScheduleV2(a.ctx, a.CustomerUUID, scheduleUUID)
}

func (a *AuthAPIClient) RestoreBackup() ybaclient.BackupsApiApiRestoreBackupV2Request {
	return a.APIClient.BackupsApi.RestoreBackupV2(a.ctx, a.CustomerUUID)
}

func (a *AuthAPIClient) ListRestores() ybaclient.BackupsApiApiListBackupRestoresV2Request {
	return a.APIClient.BackupsApi.ListBackupRestoresV2(a.ctx, a.CustomerUUID)
}

func (a *AuthAPIClient) GetRestore() ybaclient.BackupsApiApiListBackupRestoresV2Request {
	return a.APIClient.BackupsApi.ListBackupRestoresV2(a.ctx, a.CustomerUUID)
}
