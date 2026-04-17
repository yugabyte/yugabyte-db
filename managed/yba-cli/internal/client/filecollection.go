/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import ybav2client "github.com/yugabyte/platform-go-client/v2"

// CreateFileCollection triggers file collection from database nodes
func (a *AuthAPIClient) CreateFileCollection(
	uUUID string,
) ybav2client.UniverseAPICreateFileCollectionRequest {
	return a.APIv2Client.UniverseAPI.CreateFileCollection(a.ctx, a.CustomerUUID, uUUID)
}

// DownloadFileCollection downloads collected files as a tar.gz archive
func (a *AuthAPIClient) DownloadFileCollection(
	uUUID string,
	collectionUUID string,
) ybav2client.UniverseAPIDownloadFileCollectionRequest {
	return a.APIv2Client.UniverseAPI.DownloadFileCollection(
		a.ctx, a.CustomerUUID, uUUID, collectionUUID,
	)
}

// DeleteFileCollection deletes collected files from DB nodes and/or YBA
func (a *AuthAPIClient) DeleteFileCollection(
	uUUID string,
	collectionUUID string,
) ybav2client.UniverseAPIDeleteFileCollectionRequest {
	return a.APIv2Client.UniverseAPI.DeleteFileCollection(
		a.ctx, a.CustomerUUID, uUUID, collectionUUID,
	)
}
