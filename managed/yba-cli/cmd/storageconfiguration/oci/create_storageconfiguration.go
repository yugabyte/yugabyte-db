/*
 * Copyright (c) YugabyteDB, Inc.
 */

package oci

import (
	"fmt"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/storageconfiguration/storageconfigurationutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// createOCIStorageConfigurationCmd represents the storage config command
var createOCIStorageConfigurationCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add"},
	Short:   "Create an OCI YugabyteDB Anywhere storage configuration",
	Long: "Create an OCI Object Storage configuration in YugabyteDB Anywhere. " +
		"Authenticate either with OCI IAM (--use-oci-iam with --namespace) or with " +
		"OCI S3-compatible Customer Secret Keys and --s3-host-base. The key pair can be " +
		"passed with --access-key-id and --secret-access-key, or read from the " +
		"OCI_S3_ACCESS_KEY_ID and OCI_S3_SECRET_ACCESS_KEY environment variables.",
	Example: `yba storage-config oci create --name <storage-configuration-name> \
	--backup-location s3://<bucket>/<prefix> --region <oci-region> \
	--s3-host-base <namespace>.compat.objectstorage.<oci-region>.oraclecloud.com

yba storage-config oci create --name <storage-configuration-name> \
	--backup-location https://objectstorage.<oci-region>.oraclecloud.com/n/<namespace>/b/<bucket> \
	--region <oci-region> --use-oci-iam --namespace <namespace>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		storageNameFlag, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(storageNameFlag) {
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

		region, err := cmd.Flags().GetString("region")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		namespace, err := cmd.Flags().GetString("namespace")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		isIAM, err := cmd.Flags().GetBool("use-oci-iam")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		accessKeyID, err := cmd.Flags().GetString("access-key-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		secretAccessKey, err := cmd.Flags().GetString("secret-access-key")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		hostBase, err := cmd.Flags().GetString("s3-host-base")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		storageCode := util.OCIStorageConfigType

		data := map[string]interface{}{
			"BACKUP_LOCATION":          backupLocation,
			util.OCIStorageRegionField: region,
			util.UseOCIIAM:             isIAM,
		}
		if !util.IsEmptyString(namespace) {
			data[util.OCINamespaceField] = namespace
		}

		if isIAM {
			if util.IsEmptyString(namespace) {
				logrus.Fatalln(formatter.Colorize(
					"--namespace is required when --use-oci-iam is set\n", formatter.RedColor))
			}
			if !util.IsEmptyString(accessKeyID) || !util.IsEmptyString(secretAccessKey) ||
				!util.IsEmptyString(hostBase) {
				logrus.Fatalln(formatter.Colorize(
					"--access-key-id, --secret-access-key and --s3-host-base cannot be "+
						"used with --use-oci-iam\n", formatter.RedColor))
			}
		} else {
			if util.IsEmptyString(accessKeyID) && util.IsEmptyString(secretAccessKey) {
				accessKeyID, secretAccessKey, err = util.OCIS3CredentialsFromEnv()
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
			}
			if util.IsEmptyString(hostBase) {
				logrus.Fatalln(formatter.Colorize(
					"--s3-host-base is required when --use-oci-iam is not set\n",
					formatter.RedColor))
			}
			data[util.OCIS3AccessKeyIDField] = accessKeyID
			data[util.OCIS3SecretAccessKeyField] = secretAccessKey
			data[util.OCIS3HostBaseField] = hostBase
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
			util.FatalHTTPError(response, err, "Storage Configuration: OCI", "Create")
		}

		storageUUID := rCreate.GetConfigUUID()
		storageconfigurationutil.CreateStorageConfigurationUtil(authAPI, storageName, storageUUID)
	},
}

func init() {
	createOCIStorageConfigurationCmd.Flags().SortFlags = false

	createOCIStorageConfigurationCmd.Flags().String("backup-location", "",
		"[Required] The complete backup location. Accepted formats: "+
			"s3://<bucket>[/<path>] or "+
			"https://objectstorage.<region>.oraclecloud.com/n/<namespace>/b/<bucket>[/<path>].")
	createOCIStorageConfigurationCmd.MarkFlagRequired("backup-location")
	createOCIStorageConfigurationCmd.Flags().String("region", "",
		"[Required] OCI region for Object Storage, e.g. us-sanjose-1.")
	createOCIStorageConfigurationCmd.MarkFlagRequired("region")
	createOCIStorageConfigurationCmd.Flags().Bool("use-oci-iam", false,
		"[Optional] Use OCI IAM for native Object Storage access from the "+
			"YugabyteDB Anywhere host. Configuration creation will fail on insufficient "+
			"permissions on the host. (default false)")
	createOCIStorageConfigurationCmd.Flags().String("namespace", "",
		fmt.Sprintf("OCI Object Storage namespace. %s When using a native HTTPS backup "+
			"location, it must match the /n/<namespace>/ segment of the URL.",
			formatter.Colorize("Required when --use-oci-iam is set.", formatter.GreenColor)))
	createOCIStorageConfigurationCmd.Flags().String("access-key-id", "",
		fmt.Sprintf("OCI Customer Secret Key access key for the S3-compatible API. %s "+
			"Can also be set using environment variable %s.",
			formatter.Colorize(
				"Required for non IAM based storage configurations.",
				formatter.GreenColor),
			util.OCIS3AccessKeyIDField))
	createOCIStorageConfigurationCmd.Flags().String("secret-access-key", "",
		fmt.Sprintf("OCI Customer Secret Key secret for the S3-compatible API. %s "+
			"Can also be set using environment variable %s.",
			formatter.Colorize(
				"Required for non IAM based storage configurations.",
				formatter.GreenColor),
			util.OCIS3SecretAccessKeyField))
	createOCIStorageConfigurationCmd.Flags().String("s3-host-base", "",
		fmt.Sprintf("OCI S3-compatible API host base, e.g. "+
			"<namespace>.compat.objectstorage.<region>.oraclecloud.com. %s",
			formatter.Colorize(
				"Required for non IAM based storage configurations.",
				formatter.GreenColor)))
	createOCIStorageConfigurationCmd.MarkFlagsRequiredTogether(
		"access-key-id", "secret-access-key")

}
