/*
 * Copyright (c) YugabyteDB, Inc.
 */

package s3

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

// updateS3StorageConfigurationCmd represents the storage config command
var updateS3StorageConfigurationCmd = &cobra.Command{
	Use:     "update",
	Aliases: []string{"edit"},
	Short:   "Update an S3 YugabyteDB Anywhere storage configuration",
	Long:    "Update an S3 storage configuration in YugabyteDB Anywhere",
	Example: `yba storage-config s3 update --name <storage-configuration-name> \
	--new-name <new-storage-configuration-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		storageNameFlag, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(storageNameFlag) {
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
			util.FatalHTTPError(
				response,
				err,
				"Storage Configuration: S3",
				"Update - List Customer Configurations",
			)
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

		storageCode := util.S3StorageConfigType

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

		if cmd.Flags().Changed("use-iam-instance-profile") {
			isIAM, err := cmd.Flags().GetBool("use-iam-instance-profile")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			data[util.IAMInstanceProfile] = strconv.FormatBool(isIAM)
		}

		if strings.Compare(data[util.IAMInstanceProfile].(string), "true") != 0 {
			accessKeyID, err := cmd.Flags().GetString("access-key-id")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			secretAccessKey, err := cmd.Flags().GetString("secret-access-key")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if !util.IsEmptyString(accessKeyID) &&
				!util.IsEmptyString(secretAccessKey) {
				data[util.AWSAccessKeyEnv] = accessKeyID
				data[util.AWSSecretAccessKeyEnv] = secretAccessKey
			} else {
				logrus.Fatal(formatter.Colorize(
					"One of access-key-id or secret-access-key is missing", formatter.RedColor))
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
			util.FatalHTTPError(response, err, "Storage Configuration: S3", "Update")
		}

		logrus.Infof("The storage configuration %s (%s) has been updated\n",
			formatter.Colorize(storageName, formatter.GreenColor), storageUUID)

		storageconfigurationutil.UpdateStorageConfigurationUtil(authAPI, storageName, storageUUID)

	},
}

func init() {
	updateS3StorageConfigurationCmd.Flags().SortFlags = false

	// Flags needed for AWS
	updateS3StorageConfigurationCmd.PersistentFlags().String("new-name", "",
		"[Optional] Update name of the storage configuration.")
	updateS3StorageConfigurationCmd.Flags().Bool("update-credentials", false,
		"[Optional] Update credentials of the storage configuration. (default false)"+
			" If set to true, provide either (access-key-id,secret-access-key) pair"+
			" or set use-iam-instance-profile.")
	updateS3StorageConfigurationCmd.Flags().String("access-key-id", "",
		fmt.Sprintf("S3 Access Key ID. %s",
			formatter.Colorize(
				"Required for non IAM role based storage configurations.",
				formatter.GreenColor)))
	updateS3StorageConfigurationCmd.Flags().String("secret-access-key", "",
		fmt.Sprintf("S3 Secret Access Key. %s",
			formatter.Colorize(
				"Required for non IAM role based storage configurations.",
				formatter.GreenColor)))
	updateS3StorageConfigurationCmd.MarkFlagsRequiredTogether("access-key-id", "secret-access-key")
	updateS3StorageConfigurationCmd.Flags().Bool("use-iam-instance-profile", false,
		"[Optional] Use IAM Role from the YugabyteDB Anywhere Host. Configuration "+
			"creation will fail on insufficient permissions on the host. (default false)")

}
