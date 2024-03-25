/*
 * Copyright (c) YugaByte, Inc.
 */

package azure

import (
	"fmt"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/storageconfiguration/storageconfigurationutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// createAZStorageConfigurationCmd represents the storage config command
var createAZStorageConfigurationCmd = &cobra.Command{
	Use:   "create",
	Short: "Create an Azure YugabyteDB Anywhere storage configuration",
	Long:  "Create an Azure storage configuration in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		storageNameFlag, err := cmd.Flags().GetString("name")
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
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		storageName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		backupLocation, err := cmd.Flags().GetString("backup-location")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		storageCode := util.AzureStorageConfigType

		data := map[string]interface{}{
			"BACKUP_LOCATION": backupLocation,
		}

		sasToken, err := cmd.Flags().GetString("sas-token")
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
				response, err, "Storage Configuration: Azure", "Create")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}
		storageUUID := rCreate.GetConfigUUID()
		storageconfigurationutil.CreateStorageConfigurationUtil(authAPI, storageName, storageUUID)

	},
}

func init() {
	createAZStorageConfigurationCmd.Flags().SortFlags = false

	// Flags needed for Azure
	createAZStorageConfigurationCmd.Flags().String("backup-location", "",
		"[Required] The complete backup location including "+
			"\"https://<account-name>.blob.core.windows.net/<container-name>/<blob-name>\".")
	createAZStorageConfigurationCmd.MarkFlagRequired("backup-location")
	createAZStorageConfigurationCmd.Flags().String("sas-token", "",
		fmt.Sprintf("AZ SAS Token. "+
			"Can also be set using environment variable %s.",
			util.AzureStorageSasTokenEnv))

}
