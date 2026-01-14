/*
 * Copyright (c) YugabyteDB, Inc.
 */

package client

import (
	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// ListKMSConfigs fetches list of kms configs associated with the customer
func (a *AuthAPIClient) ListKMSConfigs() ybaclient.EncryptionAtRestAPIListKMSConfigsRequest {
	return a.APIClient.EncryptionAtRestAPI.ListKMSConfigs(a.ctx, a.CustomerUUID)
}

// DeleteKMSConfig deletes kms config
func (a *AuthAPIClient) DeleteKMSConfig(
	configUUID string,
) ybaclient.EncryptionAtRestAPIDeleteKMSConfigRequest {
	return a.APIClient.EncryptionAtRestAPI.DeleteKMSConfig(a.ctx, a.CustomerUUID, configUUID)
}

// CreateKMSConfig creates kms config
func (a *AuthAPIClient) CreateKMSConfig(
	providerType string,
) ybaclient.EncryptionAtRestAPICreateKMSConfigRequest {
	return a.APIClient.EncryptionAtRestAPI.CreateKMSConfig(a.ctx, a.CustomerUUID, providerType)
}

// EditKMSConfig edits kms config
func (a *AuthAPIClient) EditKMSConfig(
	configUUID string,
) ybaclient.EncryptionAtRestAPIEditKMSConfigRequest {
	return a.APIClient.EncryptionAtRestAPI.EditKMSConfig(a.ctx, a.CustomerUUID, configUUID)
}

// RefreshKMSConfig refreshes kms config
func (a *AuthAPIClient) RefreshKMSConfig(
	configUUID string,
) ybaclient.EncryptionAtRestAPIRefreshKMSConfigRequest {
	return a.APIClient.EncryptionAtRestAPI.RefreshKMSConfig(a.ctx, a.CustomerUUID, configUUID)
}

// GetListOfKMSConfigs gets list of kms configs
func (a *AuthAPIClient) GetListOfKMSConfigs(
	parentCommand, operation string,
) ([]util.KMSConfig, error) {
	kmsConfigsMap, response, err := a.ListKMSConfigs().Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(response, err,
			parentCommand, operation)
		logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}
	kmsConfigs := make([]util.KMSConfig, 0)

	if len(kmsConfigsMap) == 0 {
		return kmsConfigs, nil
	}

	for _, k := range kmsConfigsMap {
		kmsConfig, err := util.ConvertToKMSConfig(k)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		kmsConfigs = append(kmsConfigs, kmsConfig)
	}
	return kmsConfigs, nil
}
