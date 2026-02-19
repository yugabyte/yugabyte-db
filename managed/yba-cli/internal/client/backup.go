/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// CreateBackup creates backups associated with the universe
func (a *AuthAPIClient) CreateBackup() ybaclient.BackupsAPICreatebackupRequest {
	return a.APIClient.BackupsAPI.Createbackup(a.ctx, a.CustomerUUID)
}

// ListBackups associated with the customer
func (a *AuthAPIClient) ListBackups() ybaclient.BackupsAPIListBackupsV2Request {
	return a.APIClient.BackupsAPI.ListBackupsV2(a.ctx, a.CustomerUUID)
}

// GetBackupByTaskUUID fetches backups by the universe and task UUID
func (a *AuthAPIClient) GetBackupByTaskUUID(
	universeUUID string,
	taskUUID string,
) ybaclient.BackupsAPIFetchBackupsByTaskUUIDRequest {
	return a.APIClient.BackupsAPI.FetchBackupsByTaskUUID(
		a.ctx,
		a.CustomerUUID,
		universeUUID,
		taskUUID)
}

// EditBackup edits a backup
func (a *AuthAPIClient) EditBackup(backupUUID string) ybaclient.BackupsAPIEditBackupV2Request {
	return a.APIClient.BackupsAPI.EditBackupV2(a.ctx, a.CustomerUUID, backupUUID)
}

// DeleteBackups deletes the backups
func (a *AuthAPIClient) DeleteBackups() ybaclient.BackupsAPIDeleteBackupsV2Request {
	return a.APIClient.BackupsAPI.DeleteBackupsV2(a.ctx, a.CustomerUUID)
}

// ListIncrementalBackups lists the incremental backups
func (a *AuthAPIClient) ListIncrementalBackups(
	backupUUID string,
) ybaclient.BackupsAPIListIncrementalBackupsRequest {
	return a.APIClient.BackupsAPI.ListIncrementalBackups(a.ctx, a.CustomerUUID, backupUUID)
}
