/*
 * Copyright (c) YugaByte, Inc.
 */

package storageconfiguration

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

var describeStorageConfigurationCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a YugabyteDB Anywhere storage configuration",
	Long:    "Describe a storage configuration in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		storageNameFlag, err := cmd.Flags().GetString("storage-config-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(storageNameFlag)) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No storage config name found to describe\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI, err := ybaAuthClient.NewAuthAPIClient()
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		authAPI.GetCustomerUUID()
		storageConfigListRequest := authAPI.GetListOfCustomerConfig()

		r, response, err := storageConfigListRequest.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response, err, "Storage Configuration", "Describe")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		storageConfigs := make([]ybaclient.CustomerConfigUI, 0)
		for _, s := range r {
			if strings.Compare(s.GetType(), util.StorageCustomerConfigType) == 0 {
				storageConfigs = append(storageConfigs, s)
			}
		}

		// filter by name and/or by storage-configurations code
		storageName, err := cmd.Flags().GetString("storage-config-name")
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

		if len(r) > 0 && viper.GetString("output") == "table" {
			fullStorageConfigurationContext := *storageconfiguration.NewFullStorageConfigContext()
			fullStorageConfigurationContext.Output = os.Stdout
			fullStorageConfigurationContext.Format = storageconfiguration.NewFullStorageConfigFormat(viper.GetString("output"))
			fullStorageConfigurationContext.SetFullStorageConfig(r[0])
			fullStorageConfigurationContext.Write()
			return
		}

		if len(r) < 1 {
			fmt.Println("No storage configurations found")
			return
		}

		storageConfigCtx := formatter.Context{
			Output: os.Stdout,
			Format: storageconfiguration.NewStorageConfigFormat(viper.GetString("output")),
		}
		storageconfiguration.Write(storageConfigCtx, r)

	},
}

func init() {
	describeStorageConfigurationCmd.Flags().SortFlags = false
	describeStorageConfigurationCmd.Flags().StringP("storage-config-name", "n", "",
		"[Required] The name of the storage configuration to get details.")
	describeStorageConfigurationCmd.MarkFlagRequired("storage-config-name")
}
