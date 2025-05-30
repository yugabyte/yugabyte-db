/*
 * Copyright (c) YugaByte, Inc.
 */

package client

import (
	ybaclient "github.com/yugabyte/platform-go-client"
)

// RestoreBackup restores a backup
func (a *AuthAPIClient) RestoreBackup() ybaclient.BackupsApiApiRestoreBackupV2Request {
	return a.APIClient.BackupsApi.RestoreBackupV2(a.ctx, a.CustomerUUID)
}

// ListRestores associated with the customer
func (a *AuthAPIClient) ListRestores() ybaclient.BackupsApiApiListBackupRestoresV2Request {
	return a.APIClient.BackupsApi.ListBackupRestoresV2(a.ctx, a.CustomerUUID)
}

// GetRestore fetches restores of the customer
func (a *AuthAPIClient) GetRestore() ybaclient.BackupsApiApiListBackupRestoresV2Request {
	return a.APIClient.BackupsApi.ListBackupRestoresV2(a.ctx, a.CustomerUUID)
}
