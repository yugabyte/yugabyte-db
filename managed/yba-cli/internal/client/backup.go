/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// CreateBackup creates backups associated with the universe
func (a *AuthAPIClient) CreateBackup() ybaclient.BackupsApiApiCreatebackupRequest {
	return a.APIClient.BackupsApi.Createbackup(a.ctx, a.CustomerUUID)
}

// ListBackups associated with the customer
func (a *AuthAPIClient) ListBackups() ybaclient.BackupsApiApiListBackupsV2Request {
	return a.APIClient.BackupsApi.ListBackupsV2(a.ctx, a.CustomerUUID)
}

// GetBackupByTaskUUID fetches backups by the universe and task UUID
func (a *AuthAPIClient) GetBackupByTaskUUID(
	universeUUID string,
	taskUUID string,
) ybaclient.BackupsApiApiFetchBackupsByTaskUUIDRequest {
	return a.APIClient.BackupsApi.FetchBackupsByTaskUUID(
		a.ctx,
		a.CustomerUUID,
		universeUUID,
		taskUUID)
}

// EditBackup edits a backup
func (a *AuthAPIClient) EditBackup(backupUUID string) ybaclient.BackupsApiApiEditBackupV2Request {
	return a.APIClient.BackupsApi.EditBackupV2(a.ctx, a.CustomerUUID, backupUUID)
}

// DeleteBackups deletes the backups
func (a *AuthAPIClient) DeleteBackups() ybaclient.BackupsApiApiDeleteBackupsV2Request {
	return a.APIClient.BackupsApi.DeleteBackupsV2(a.ctx, a.CustomerUUID)
}

// ListIncrementalBackups lists the incremental backups
func (a *AuthAPIClient) ListIncrementalBackups(
	backupUUID string,
) ybaclient.BackupsApiApiListIncrementalBackupsRequest {
	return a.APIClient.BackupsApi.ListIncrementalBackups(a.ctx, a.CustomerUUID, backupUUID)
}
