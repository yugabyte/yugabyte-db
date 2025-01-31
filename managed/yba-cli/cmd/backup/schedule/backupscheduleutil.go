/*
 * Copyright (c) YugaByte, Inc.
 */

package schedule

import (
	"strings"

	"github.com/sirupsen/logrus"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/backup/schedule"
)

func fetchStorageAndKMSConfigurationsForListing(
	authAPI *ybaAuthClient.AuthAPIClient,
	operation string,
) {
	storageConfigListRequest := authAPI.GetListOfCustomerConfig()
	rList, response, err := storageConfigListRequest.Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(
			response, err, "Backup Schedule", operation+" - Get Storage Configuration")
		logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}

	schedule.StorageConfigs = make([]ybaclient.CustomerConfigUI, 0)
	for _, s := range rList {
		if strings.Compare(s.GetType(), util.StorageCustomerConfigType) == 0 {
			schedule.StorageConfigs = append(schedule.StorageConfigs, s)
		}
	}

	schedule.KMSConfigs = make([]util.KMSConfig, 0)
	kmsConfigs, response, err := authAPI.ListKMSConfigs().Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(response, err,
			"Backup Schedule", operation+" - Get KMS Configurations")
		logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}

	for _, k := range kmsConfigs {
		kmsConfig, err := util.ConvertToKMSConfig(k)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		schedule.KMSConfigs = append(schedule.KMSConfigs, kmsConfig)
	}
}
