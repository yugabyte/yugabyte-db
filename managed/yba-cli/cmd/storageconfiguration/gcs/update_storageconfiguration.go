/*
 * Copyright (c) YugaByte, Inc.
 */

package gcs

import (
	"fmt"
	"strconv"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/storageconfiguration/storageconfigurationutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// updateGCSStorageConfigurationCmd represents the storage config command
var updateGCSStorageConfigurationCmd = &cobra.Command{
	Use:   "update",
	Short: "Update an GCS YugabyteDB Anywhere storage configuration",
	Long:  "Update an GCS storage configuration in YugabyteDB Anywhere",
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
				response, err, "Storage Configuration: GCS",
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

		storageCode := util.GCSStorageConfigType

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

		updateCredentials, err := cmd.Flags().GetBool("update-credentials")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if updateCredentials {
			isIAM, err := cmd.Flags().GetBool("use-gcp-iam")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if isIAM {
				data[util.UseGCPIAM] = strconv.FormatBool(isIAM)
			} else {
				gcsFilePath, err := cmd.Flags().GetString("credentials-file-path")
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
				var gcsCreds string
				if len(gcsFilePath) == 0 {
					gcsCreds, err = util.GcpGetCredentialsAsString()
					if err != nil {
						logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
					}

				} else {
					gcsCreds, err = util.GcpGetCredentialsAsStringFromFilePath(gcsFilePath)
					if err != nil {
						logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
					}
				}
				data[util.GCSCredentialsJSON] = gcsCreds
			}
		}

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
				response, err, "Storage Configuration: GCS", "Update")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		logrus.Infof("The storage configuration %s (%s) has been updated\n",
			formatter.Colorize(storageName, formatter.GreenColor), storageUUID)

		storageconfigurationutil.UpdateStorageConfigurationUtil(authAPI, storageName, storageUUID)

	},
}

func init() {
	updateGCSStorageConfigurationCmd.Flags().SortFlags = false

	// Flags needed for GCS
	updateGCSStorageConfigurationCmd.PersistentFlags().String("new-name", "",
		"[Optional] Update name of the storage configuration.")
	updateGCSStorageConfigurationCmd.Flags().Bool("update-credentials", false,
		"[Optional] Update credentials of the storage configuration. (default false)"+
			" If set to true, provide either credentials-file-path"+
			" or set use-gcp-iam.")
	updateGCSStorageConfigurationCmd.Flags().String("credentials-file-path", "",
		fmt.Sprintf("GCS Credentials File Path. %s "+
			"Can also be set using environment variable %s.",
			formatter.Colorize(
				"Required for non IAM role based storage configurations.",
				formatter.GreenColor),
			util.GCPCredentialsEnv))
	updateGCSStorageConfigurationCmd.Flags().Bool("use-gcp-iam", false,
		"[Optional] Use IAM Role from the YugabyteDB Anywhere Host. "+
			"Supported for Kubernetes GKE clusters with workload identity. Configuration "+
			"creation will fail on insufficient permissions on the host. (default false)")

}
