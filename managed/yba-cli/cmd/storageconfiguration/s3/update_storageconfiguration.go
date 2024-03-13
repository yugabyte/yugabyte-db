/*
 * Copyright (c) YugaByte, Inc.
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
	Use:   "update",
	Short: "Update an S3 YugabyteDB Anywhere storage configuration",
	Long:  "Update an S3 storage configuration in YugabyteDB Anywhere",
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
				response, err, "Storage Configuration: S3",
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
			fmt.Println("No storage configurations found")
			return
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

		updateCredentials, err := cmd.Flags().GetBool("update-credentials")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if updateCredentials {
			isIAM, err := cmd.Flags().GetBool("use-iam-instance-profile")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}

			if isIAM {
				data[util.IAMInstanceProfile] = strconv.FormatBool(isIAM)
			} else {
				accessKeyID, err := cmd.Flags().GetString("access-key-id")
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
				secretAccessKey, err := cmd.Flags().GetString("secret-access-key")
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
				if len(accessKeyID) == 0 && len(secretAccessKey) == 0 {
					awsCreds, err := util.AwsCredentialsFromEnv()
					if err != nil {
						logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
					}
					data[util.AWSAccessKeyEnv] = awsCreds.AccessKeyID
					data[util.AWSSecretAccessKeyEnv] = awsCreds.SecretAccessKey
				} else {
					data[util.AWSAccessKeyEnv] = accessKeyID
					data[util.AWSSecretAccessKeyEnv] = secretAccessKey
				}
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
				response, err, "Storage Configuration: S3", "Update")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		fmt.Printf("The storage configuration %s (%s) has been updated\n",
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
		"[Optional] Update credentials of the storage configuration, defaults to false."+
			" If set to true, provide either (access-key-id,secret-access-key) pair"+
			" or set use-iam-instance-profile.")
	updateS3StorageConfigurationCmd.Flags().String("access-key-id", "",
		fmt.Sprintf("S3 Access Key ID. %s "+
			"Can also be set using environment variable %s.",
			formatter.Colorize(
				"Required for non IAM role based storage configurations.",
				formatter.GreenColor),
			util.AWSAccessKeyEnv))
	updateS3StorageConfigurationCmd.Flags().String("secret-access-key", "",
		fmt.Sprintf("S3 Secret Access Key. %s "+
			"Can also be set using environment variable %s.",
			formatter.Colorize(
				"Required for non IAM role based storage configurations.",
				formatter.GreenColor),
			util.AWSSecretAccessKeyEnv))
	updateS3StorageConfigurationCmd.MarkFlagsRequiredTogether("access-key-id", "secret-access-key")
	updateS3StorageConfigurationCmd.Flags().Bool("use-iam-instance-profile", false,
		"[Optional] Use IAM Role from the YugabyteDB Anywhere Host. Configuration "+
			"creation will fail on insufficient permissions on the host, defaults to false.")

}
