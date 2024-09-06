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

func DescribeStorageConfigurationUtil(cmd *cobra.Command, commandCall string) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

	storageConfigListRequest := authAPI.GetListOfCustomerConfig()

	r, response, err := storageConfigListRequest.Execute()
	if err != nil {
		callSite := "Storage Configuration"
		if len(strings.TrimSpace(commandCall)) != 0 {
			callSite = fmt.Sprintf("%s: %s", callSite, commandCall)
		}
		errMessage := util.ErrorFromHTTPResponse(
			response, err, callSite, "Describe")
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

	r = storageConfigs

	if len(r) > 0 && util.IsOutputType(formatter.TableFormatKey) {
		fullStorageConfigurationContext := *storageconfiguration.NewFullStorageConfigContext()
		fullStorageConfigurationContext.Output = os.Stdout
		fullStorageConfigurationContext.Format = storageconfiguration.
			NewFullStorageConfigFormat(viper.GetString("output"))
		fullStorageConfigurationContext.SetFullStorageConfig(r[0])
		fullStorageConfigurationContext.Write()
		return
	}

	if len(r) < 1 {
		logrus.Fatalf(
			formatter.Colorize(
				fmt.Sprintf("No storage configurations with name: %s found\n",
					storageName),
				formatter.RedColor,
			))
	}

	storageConfigCtx := formatter.Context{
		Command: "describe",
		Output:  os.Stdout,
		Format:  storageconfiguration.NewStorageConfigFormat(viper.GetString("output")),
	}
	storageconfiguration.Write(storageConfigCtx, r)

}

func DescribeStorageConfigurationValidation(cmd *cobra.Command) {
	storageNameFlag, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if len(strings.TrimSpace(storageNameFlag)) == 0 {
		cmd.Help()
		logrus.Fatalln(
			formatter.Colorize("No storage config name found to describe\n", formatter.RedColor))
	}
}
