/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// RestoreBackup restores a backup
func (a *AuthAPIClient) RestoreBackup() ybaclient.BackupsAPIRestoreBackupV2Request {
	return a.APIClient.BackupsAPI.RestoreBackupV2(a.ctx, a.CustomerUUID)
}

// ListRestores associated with the customer
func (a *AuthAPIClient) ListRestores() ybaclient.BackupsAPIListBackupRestoresV2Request {
	return a.APIClient.BackupsAPI.ListBackupRestoresV2(a.ctx, a.CustomerUUID)
}

// GetRestore fetches restores of the customer
func (a *AuthAPIClient) GetRestore() ybaclient.BackupsAPIListBackupRestoresV2Request {
	return a.APIClient.BackupsAPI.ListBackupRestoresV2(a.ctx, a.CustomerUUID)
}
