/*
 * Copyright (c) YugaByte, Inc.
 */

package nfs

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

// updateNFSStorageConfigurationCmd represents the storage config command
var updateNFSStorageConfigurationCmd = &cobra.Command{
	Use:     "update",
	Aliases: []string{"edit"},
	Short:   "Update an NFS YugabyteDB Anywhere storage configuration",
	Long:    "Update an NFS storage configuration in YugabyteDB Anywhere",
	Example: `yba storage-config nfs update --name <storage-configuration-name> \
	--new-name <new-storage-configuration-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		storageNameFlag, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(storageNameFlag)) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize(
					"No storage configuration name found to update\n",
					formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		storageConfigListRequest := authAPI.GetListOfCustomerConfig()

		r, response, err := storageConfigListRequest.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response, err, "Storage Configuration: NFS",
				"Update - List Customer Configurations")
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

		if len(r) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No storage configurations with name: %s found\n",
						storageName),
					formatter.RedColor,
				))
		}

		var storageConfig ybaclient.CustomerConfigUI
		if len(r) > 0 {
			storageConfig = r[0]
		}

		storageUUID := storageConfig.GetConfigUUID()

		storageCode := util.NFSStorageConfigType

		newStorageName, err := cmd.Flags().GetString("new-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(newStorageName) > 0 {
			storageName = newStorageName
		}

		if strings.Compare(storageConfig.GetName(), storageCode) != 0 {
			err := fmt.Sprintf("Incorrect command to edit selected storage configuration."+
				" Use %s command.", strings.ToLower(storageConfig.GetName()))
			logrus.Fatalf(formatter.Colorize(err+"\n", formatter.RedColor))
		}

		data := storageConfig.GetData()

		requestBody := ybaclient.CustomerConfig{
			Name:         storageCode,
			CustomerUUID: authAPI.CustomerUUID,
			ConfigName:   storageName,
			Type:         util.StorageCustomerConfigType,
			Data:         data,
		}

		_, response, err = authAPI.EditCustomerConfig(storageUUID).
			Config(requestBody).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response, err, "Storage Configuration: NFS", "Update")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		logrus.Infof("The storage configuration %s (%s) has been updated\n",
			formatter.Colorize(storageName, formatter.GreenColor), storageUUID)

		storageconfigurationutil.UpdateStorageConfigurationUtil(authAPI, storageName, storageUUID)

	},
}

func init() {
	updateNFSStorageConfigurationCmd.Flags().SortFlags = false
	updateNFSStorageConfigurationCmd.PersistentFlags().String("new-name", "",
		"[Optional] Update name of the storage configuration.")

}
