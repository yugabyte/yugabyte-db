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

		wasIAM := util.IsTrueValue(data[util.IAMInstanceProfile])
		isIAM := wasIAM
		if cmd.Flags().Changed("use-iam-instance-profile") {
			isIAM, err = cmd.Flags().GetBool("use-iam-instance-profile")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			data[util.IAMInstanceProfile] = strconv.FormatBool(isIAM)
		}

		accessKeyID, err := cmd.Flags().GetString("access-key-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		secretAccessKey, err := cmd.Flags().GetString("secret-access-key")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		keyFlagSet := cmd.Flags().Changed("access-key-id") ||
			cmd.Flags().Changed("secret-access-key")
		updateCreds, err := cmd.Flags().GetBool("update-credentials")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		err = applyS3Keys(
			data, accessKeyID, secretAccessKey, keyFlagSet, updateCreds, wasIAM, isIAM)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
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

// applyS3Keys writes the S3 key pair into data. Without key flags the stored pair is kept,
// or read from the environment with --update-credentials or when switching off IAM with no
// stored keys.
// Switching to IAM drops the keys; --update-credentials is ignored with IAM.
func applyS3Keys(
	data map[string]interface{},
	accessKeyID, secretAccessKey string,
	keyFlagSet, updateCreds, wasIAM, isIAM bool,
) error {
	if isIAM {
		if keyFlagSet {
			return fmt.Errorf("--access-key-id and --secret-access-key cannot be used " +
				"with IAM instance profile based storage configurations")
		}
		if updateCreds {
			logrus.Warnln("--update-credentials is ignored for IAM based storage configurations")
		}
		if !wasIAM {
			delete(data, util.AWSAccessKeyEnv)
			delete(data, util.AWSSecretAccessKeyEnv)
		}
		return nil
	}
	if !keyFlagSet {
		hasStored := util.HasValue(data[util.AWSAccessKeyEnv])
		if !updateCreds && (!wasIAM || hasStored) {
			return nil
		}
		logrus.Infof("Reading S3 keys from %s and %s\n",
			util.AWSAccessKeyEnv, util.AWSSecretAccessKeyEnv)
		awsCreds, err := util.AwsCredentialsFromEnv()
		if err != nil {
			return err
		}
		accessKeyID, secretAccessKey = awsCreds.AccessKeyID, awsCreds.SecretAccessKey
	} else if util.IsEmptyString(accessKeyID) || util.IsEmptyString(secretAccessKey) {
		return fmt.Errorf("--access-key-id and --secret-access-key must both be non-empty")
	}
	data[util.AWSAccessKeyEnv] = accessKeyID
	data[util.AWSSecretAccessKeyEnv] = secretAccessKey
	return nil
}

func init() {
	updateS3StorageConfigurationCmd.Flags().SortFlags = false

	// Flags needed for AWS
	updateS3StorageConfigurationCmd.PersistentFlags().String("new-name", "",
		"[Optional] Update name of the storage configuration.")
	updateS3StorageConfigurationCmd.Flags().Bool("update-credentials", false,
		"[Optional] Read new keys from AWS_ACCESS_KEY_ID and AWS_SECRET_ACCESS_KEY"+
			" when --access-key-id and --secret-access-key are not given. (default false)")
	updateS3StorageConfigurationCmd.Flags().String("access-key-id", "",
		fmt.Sprintf("S3 Access Key ID. %s",
			formatter.Colorize(
				"Omit to keep the stored keys. Switching off IAM with no stored keys "+
					"reads them from the environment.",
				formatter.GreenColor)))
	updateS3StorageConfigurationCmd.Flags().String("secret-access-key", "",
		fmt.Sprintf("S3 Secret Access Key. %s",
			formatter.Colorize(
				"Omit to keep the stored keys. Switching off IAM with no stored keys "+
					"reads them from the environment.",
				formatter.GreenColor)))
	updateS3StorageConfigurationCmd.MarkFlagsRequiredTogether("access-key-id", "secret-access-key")
	updateS3StorageConfigurationCmd.Flags().Bool("use-iam-instance-profile", false,
		"[Optional] Use IAM Role from the YugabyteDB Anywhere Host. Configuration "+
			"creation will fail on insufficient permissions on the host. (default false)")

}
