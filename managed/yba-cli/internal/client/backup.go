/*
 * Copyright (c) YugaByte, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// CreateBackup creates backups associated with the universe
func (a *AuthAPIClient) CreateBackup() (
	ybaclient.BackupsApiApiCreatebackupRequest) {
	return a.APIClient.BackupsApi.Createbackup(a.ctx, a.CustomerUUID)
}

// ListBackups associated with the customer
func (a *AuthAPIClient) ListBackups() (
	ybaclient.BackupsApiApiListBackupsV2Request) {
	return a.APIClient.BackupsApi.ListBackupsV2(a.ctx, a.CustomerUUID)
}

// GetBackupByTaskUUID fetches backups by the universe and task UUID
func (a *AuthAPIClient) GetBackupByTaskUUID(universeUUID string, taskUUID string) (
	ybaclient.BackupsApiApiFetchBackupsByTaskUUIDRequest) {
	return a.APIClient.BackupsApi.FetchBackupsByTaskUUID(
		a.ctx,
		a.CustomerUUID,
		universeUUID,
		taskUUID)
}

// EditBackup edits a backup
func (a *AuthAPIClient) EditBackup(backupUUID string) (
	ybaclient.BackupsApiApiEditBackupV2Request) {
	return a.APIClient.BackupsApi.EditBackupV2(a.ctx, a.CustomerUUID, backupUUID)
}

// DeleteBackups deletes the backups
func (a *AuthAPIClient) DeleteBackups() (
	ybaclient.BackupsApiApiDeleteBackupsV2Request) {
	return a.APIClient.BackupsApi.DeleteBackupsV2(a.ctx, a.CustomerUUID)
}

// ListIncrementalBackups lists the incremental backups
func (a *AuthAPIClient) ListIncrementalBackups(backupUUID string) (
	ybaclient.BackupsApiApiListIncrementalBackupsRequest) {
	return a.APIClient.BackupsApi.ListIncrementalBackups(a.ctx, a.CustomerUUID, backupUUID)
}

// CreateBackupSchedule creates a backup schedule
func (a *AuthAPIClient) CreateBackupSchedule() (
	ybaclient.BackupsApiApiCreateBackupScheduleAsyncRequest) {
	return a.APIClient.BackupsApi.CreateBackupScheduleAsync(a.ctx, a.CustomerUUID)
}

// ListBackupSchedules associated with the customer
func (a *AuthAPIClient) ListBackupSchedules() (
	ybaclient.ScheduleManagementApiApiListSchedulesV2Request) {
	return a.APIClient.ScheduleManagementApi.ListSchedulesV2(a.ctx, a.CustomerUUID)
}

// GetBackupSchedule fetches backups by the universe and task UUID
func (a *AuthAPIClient) GetBackupSchedule(scheduleUUID string) (
	ybaclient.ScheduleManagementApiApiGetScheduleRequest) {
	return a.APIClient.ScheduleManagementApi.GetSchedule(a.ctx, a.CustomerUUID, scheduleUUID)
}

// DeleteBackupSchedule deletes the backup schedule
func (a *AuthAPIClient) DeleteBackupSchedule(scheduleUUID string) (
	ybaclient.ScheduleManagementApiApiDeleteScheduleV2Request) {
	return a.APIClient.ScheduleManagementApi.DeleteScheduleV2(a.ctx, a.CustomerUUID, scheduleUUID)
}

// EditBackupSchedule edits a backup schedule
func (a *AuthAPIClient) EditBackupSchedule(scheduleUUID string) (
	ybaclient.ScheduleManagementApiApiEditBackupScheduleV2Request) {
	return a.APIClient.ScheduleManagementApi.EditBackupScheduleV2(
		a.ctx,
		a.CustomerUUID,
		scheduleUUID)
}

// RestoreBackup restores a backup
func (a *AuthAPIClient) RestoreBackup() (
	ybaclient.BackupsApiApiRestoreBackupV2Request) {
	return a.APIClient.BackupsApi.RestoreBackupV2(a.ctx, a.CustomerUUID)
}

// ListRestores associated with the customer
func (a *AuthAPIClient) ListRestores() (
	ybaclient.BackupsApiApiListBackupRestoresV2Request) {
	return a.APIClient.BackupsApi.ListBackupRestoresV2(a.ctx, a.CustomerUUID)
}

// GetRestore fetches restores of the customer
func (a *AuthAPIClient) GetRestore() (
	ybaclient.BackupsApiApiListBackupRestoresV2Request) {
	return a.APIClient.BackupsApi.ListBackupRestoresV2(a.ctx, a.CustomerUUID)
}
