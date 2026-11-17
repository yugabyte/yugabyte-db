/*
 * Copyright (c) YugabyteDB, Inc.
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

// FilterStorageConfigs returns the storage configs in configs with the given name and storage
// code. An empty name or storageCode matches any.
func FilterStorageConfigs(
	configs []ybaclient.CustomerConfigUI, name, storageCode string,
) []ybaclient.CustomerConfigUI {
	filtered := make([]ybaclient.CustomerConfigUI, 0)
	for _, s := range configs {
		if strings.Compare(s.GetType(), util.StorageCustomerConfigType) != 0 {
			continue
		}
		if name != "" && strings.Compare(s.GetConfigName(), name) != 0 {
			continue
		}
		if !util.IsEmptyString(storageCode) && !strings.EqualFold(s.GetName(), storageCode) {
			continue
		}
		filtered = append(filtered, s)
	}
	return filtered
}

func storageConfigNotFoundMessage(name, storageCode string) string {
	if util.IsEmptyString(storageCode) {
		return fmt.Sprintf("No storage configurations with name: %s found\n", name)
	}
	return fmt.Sprintf("No %s storage configurations with name: %s found\n", storageCode, name)
}

func DescribeStorageConfigurationUtil(cmd *cobra.Command, commandCall, storageCode string) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

	storageConfigListRequest := authAPI.GetListOfCustomerConfig()

	r, response, err := storageConfigListRequest.Execute()
	if err != nil {
		callSite := "Storage Configuration"
		if !util.IsEmptyString(commandCall) {
			callSite = fmt.Sprintf("%s: %s", callSite, commandCall)
		}
		util.FatalHTTPError(response, err, callSite, "Describe")
	}

	storageName, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	r = FilterStorageConfigs(r, storageName, storageCode)

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
		logrus.Fatalf(formatter.Colorize(
			storageConfigNotFoundMessage(storageName, storageCode), formatter.RedColor))
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
	if util.IsEmptyString(storageNameFlag) {
		cmd.Help()
		logrus.Fatalln(
			formatter.Colorize("No storage config name found to describe\n", formatter.RedColor))
	}
}
