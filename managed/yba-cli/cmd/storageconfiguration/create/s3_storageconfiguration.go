/*
 * Copyright (c) YugaByte, Inc.
 */

package create

import (
	"fmt"
	"strconv"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// createS3StorageConfigurationCmd represents the storage config command
var createS3StorageConfigurationCmd = &cobra.Command{
	Use:   "s3",
	Short: "Create an S3 YugabyteDB Anywhere storage configuration",
	Long:  "Create an S3 storage configuration in YugabyteDB Anywhere",
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

		storageCode := "S3"

		data := map[string]interface{}{
			"BACKUP_LOCATION": backupLocation,
		}
		isIAM, err := cmd.Flags().GetBool("use-iam-instance-profile")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if isIAM {
			data[util.IAMInstanceProfile] = strconv.FormatBool(isIAM)
		} else {
			accessKeyID, err := cmd.Flags().GetString("s3-access-key-id")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			secretAccessKey, err := cmd.Flags().GetString("s3-secret-access-key")
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
				response, err, "Storage Configuration", "Create S3")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		storageUUID := rCreate.GetConfigUUID()
		createStorageConfigurationUtil(authAPI, storageName, storageUUID)
	},
}

func init() {
	createS3StorageConfigurationCmd.Flags().SortFlags = false

	// Flags needed for AWS
	createS3StorageConfigurationCmd.Flags().String("s3-access-key-id", "",
		fmt.Sprintf("S3 Access Key ID. %s "+
			"Can also be set using environment variable %s.",
			formatter.Colorize(
				"Required for non IAM role based storage configurations.",
				formatter.GreenColor),
			util.AWSAccessKeyEnv))
	createS3StorageConfigurationCmd.Flags().String("s3-secret-access-key", "",
		fmt.Sprintf("S3 Secret Access Key. %s "+
			"Can also be set using environment variable %s.",
			formatter.Colorize(
				"Required for non IAM role based storage configurations.",
				formatter.GreenColor),
			util.AWSSecretAccessKeyEnv))
	createS3StorageConfigurationCmd.MarkFlagsRequiredTogether("s3-access-key-id", "s3-secret-access-key")
	createS3StorageConfigurationCmd.Flags().Bool("use-iam-instance-profile", false,
		"[Optional] Use IAM Role from the YugabyteDB Anywhere Host. Configuration "+
			"creation will fail on insufficient permissions on the host, defaults to false.")

}
