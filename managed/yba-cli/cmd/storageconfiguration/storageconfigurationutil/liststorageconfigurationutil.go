/*
 * Copyright (c) YugaByte, Inc.
 */

package storageconfigurationutil

import (
	"fmt"
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/storageconfiguration"
)

func ListStorageConfigurationUtil(cmd *cobra.Command, commandCall, storageCode string) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

	storageListRequest := authAPI.GetListOfCustomerConfig()

	r, response, err := storageListRequest.Execute()
	if err != nil {
		callSite := "Storage Configuration"
		if len(strings.TrimSpace(commandCall)) != 0 {
			callSite = fmt.Sprintf("%s: %s", callSite, commandCall)
		}
		errMessage := util.ErrorFromHTTPResponse(
			response, err, callSite, "List")
		logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}

	storageConfigs := make([]ybaclient.CustomerConfigUI, 0)
	for _, s := range r {
		if strings.Compare(s.GetType(), util.StorageCustomerConfigType) == 0 {
			storageConfigs = append(storageConfigs, s)
		}
	}

	// filter by name and/or by storage-configurations code
	storageName, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if storageName != "" {
		storageConfigsName := make([]ybaclient.CustomerConfigUI, 0)
		for _, s := range storageConfigs {
			if strings.Compare(s.GetConfigName(), storageName) == 0 {
				storageConfigsName = append(storageConfigsName, s)
			}
		}
		storageConfigs = storageConfigsName
	}
	var codes []string
	if len(strings.TrimSpace(storageCode)) != 0 {
		codes = []string{strings.ToUpper(storageCode)}
	} else if len(strings.TrimSpace(commandCall)) == 0 {
		codes = []string{util.S3StorageConfigType, util.GCSStorageConfigType, util.AzureStorageConfigType, util.NFSStorageConfigType}
	}
	storageConfigsCode := make([]ybaclient.CustomerConfigUI, 0)
	for _, c := range codes {
		for _, s := range storageConfigs {
			if strings.Compare(s.GetName(), c) == 0 {
				storageConfigsCode = append(storageConfigsCode, s)
			}
		}
	}
	storageConfigs = storageConfigsCode

	storageCtx := formatter.Context{
		Output: os.Stdout,
		Format: storageconfiguration.NewStorageConfigFormat(viper.GetString("output")),
	}
	if len(storageConfigs) < 1 {
		fmt.Println("No storage configurations found")
		return
	}
	storageconfiguration.Write(storageCtx, storageConfigs)

}
