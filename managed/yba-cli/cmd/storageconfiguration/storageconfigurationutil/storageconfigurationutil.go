/*
 * Copyright (c) YugaByte, Inc.
 */

package storageconfigurationutil

import (
	"fmt"
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	storageConfigFormatter "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/storageconfiguration"
)

// CreateStorageConfigurationUtil is util function for task progress
func CreateStorageConfigurationUtil(
	authAPI *ybaAuthClient.AuthAPIClient, storageName, storageUUID string,
) {
	fmt.Printf("The storage configuration %s (%s) has been created\n",
		formatter.Colorize(storageName, formatter.GreenColor), storageUUID)

	storageConfigData, response, err := authAPI.GetListOfCustomerConfig().Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(
			response,
			err,
			"Storage Configuration",
			"Create - List Storage Configuration")
		logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}
	storageConfigsUUID := make([]ybaclient.CustomerConfigUI, 0)
	for _, s := range storageConfigData {
		if strings.Compare(s.GetConfigUUID(), storageUUID) == 0 {
			storageConfigsUUID = append(storageConfigsUUID, s)
		}
	}
	storageConfigData = storageConfigsUUID
	storageConfigsCtx := formatter.Context{
		Output: os.Stdout,
		Format: storageConfigFormatter.NewStorageConfigFormat(viper.GetString("output")),
	}
	storageConfigFormatter.Write(storageConfigsCtx, storageConfigData)

}

// UpdateStorageConfigurationUtil is util function for task progress
func UpdateStorageConfigurationUtil(
	authAPI *ybaAuthClient.AuthAPIClient, storageName, storageUUID string,
) {
	storageConfigData, response, err := authAPI.GetListOfCustomerConfig().Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(
			response,
			err,
			"Storage Configuration",
			"Update - List Storage Configuration")
		logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}
	storageConfigsUUID := make([]ybaclient.CustomerConfigUI, 0)
	for _, s := range storageConfigData {
		if strings.Compare(s.GetConfigUUID(), storageUUID) == 0 {
			storageConfigsUUID = append(storageConfigsUUID, s)
		}
	}
	storageConfigData = storageConfigsUUID
	storageConfigsCtx := formatter.Context{
		Output: os.Stdout,
		Format: storageConfigFormatter.NewStorageConfigFormat(viper.GetString("output")),
	}

	storageConfigFormatter.Write(storageConfigsCtx, storageConfigData)
}
