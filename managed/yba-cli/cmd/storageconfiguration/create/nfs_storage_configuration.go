/*
 * Copyright (c) YugaByte, Inc.
 */

package create

import (
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// createNFSStorageConfigurationCmd represents the storage config command
var createNFSStorageConfigurationCmd = &cobra.Command{
	Use:   "nfs",
	Short: "Create an NFS YugabyteDB Anywhere storage configuration",
	Long:  "Create an NFS storage configuration in YugabyteDB Anywhere",
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

		storageCode := "NFS"

		data := map[string]interface{}{
			"BACKUP_LOCATION": backupLocation,
		}

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
				response, err, "Storage Configuration", "Create NFS")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		storageUUID := rCreate.GetConfigUUID()
		createStorageConfigurationUtil(authAPI, storageName, storageUUID)

	},
}

func init() {
	createNFSStorageConfigurationCmd.Flags().SortFlags = false

}
