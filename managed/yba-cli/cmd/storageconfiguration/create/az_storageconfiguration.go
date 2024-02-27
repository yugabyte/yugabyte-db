/*
 * Copyright (c) YugaByte, Inc.
 */

package create

import (
	"fmt"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// createAZStorageConfigurationCmd represents the storage config command
var createAZStorageConfigurationCmd = &cobra.Command{
	Use:   "azure",
	Short: "Create an Azure YugabyteDB Anywhere storage configuration",
	Long:  "Create an Azure storage configuration in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		storageNameFlag, err := cmd.Flags().GetString("storage-config-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(storageNameFlag)) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize(
					"No storage configuration name found to create\n",
					formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI, err := ybaAuthClient.NewAuthAPIClient()
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		authAPI.GetCustomerUUID()
		storageName, err := cmd.Flags().GetString("storage-config-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		backupLocation, err := cmd.Flags().GetString("backup-location")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		storageCode := "AZ"

		data := map[string]interface{}{
			"BACKUP_LOCATION": backupLocation,
		}

		sasToken, err := cmd.Flags().GetString("az-sas-token")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(sasToken) == 0 {

			sasToken, err = util.AzureStorageCredentialsFromEnv()
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}

		}
		data[util.AzureStorageSasTokenEnv] = sasToken

		requestBody := ybaclient.CustomerConfig{
			Name:         storageCode,
			CustomerUUID: authAPI.CustomerUUID,
			ConfigName:   storageName,
			Type:         util.StorageCustomerConfigType,
			Data:         data,
		}

		rCreate, response, err := authAPI.CreateCustomerConfig().
			Config(requestBody).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response, err, "Storage Configuration", "Create AZ")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}
		storageUUID := rCreate.GetConfigUUID()
		createStorageConfigurationUtil(authAPI, storageName, storageUUID)

	},
}

func init() {
	createAZStorageConfigurationCmd.Flags().SortFlags = false

	// Flags needed for Azure
	createAZStorageConfigurationCmd.Flags().String("az-sas-token", "",
		fmt.Sprintf("AZ SAS Token. "+
			"Can also be set using environment variable %s.",
			util.AzureStorageSasTokenEnv))

}
