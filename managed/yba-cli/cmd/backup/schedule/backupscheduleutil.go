/*
 * Copyright (c) YugabyteDB, Inc.
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
		util.FatalHTTPError(
			response,
			err,
			"Backup Schedule",
			operation+" - Get Storage Configuration",
		)
	}

	schedule.StorageConfigs = make([]ybaclient.CustomerConfigUI, 0)
	for _, s := range rList {
		if strings.Compare(s.GetType(), util.StorageCustomerConfigType) == 0 {
			schedule.StorageConfigs = append(schedule.StorageConfigs, s)
		}
	}

	schedule.KMSConfigs, err = authAPI.GetListOfKMSConfigs(
		"Backup Schedule", operation+" - Get KMS Configurations")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
}
