/*
 * Copyright (c) YugabyteDB, Inc.
 */

package oci

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

// updateOCIStorageConfigurationCmd represents the storage config command
var updateOCIStorageConfigurationCmd = &cobra.Command{
	Use:     "update",
	Aliases: []string{"edit"},
	Short:   "Update an OCI YugabyteDB Anywhere storage configuration",
	Long:    "Update an OCI storage configuration in YugabyteDB Anywhere",
	Example: `yba storage-config oci update --name <storage-configuration-name> \
	--new-name <new-storage-configuration-name>

yba storage-config oci update --name <storage-configuration-name> \
	--use-oci-iam=false --s3-host-base <namespace>.compat.objectstorage.<region>.oraclecloud.com`,
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
				"Storage Configuration: OCI",
				"Update - List Customer Configurations",
			)
		}

		storageConfigs := make([]ybaclient.CustomerConfigUI, 0)
		for _, s := range r {
			if strings.Compare(s.GetType(), util.StorageCustomerConfigType) == 0 {
				storageConfigs = append(storageConfigs, s)
			}
		}

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

		storageConfig := r[0]
		storageUUID := storageConfig.GetConfigUUID()

		storageCode := util.OCIStorageConfigType

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

		if cmd.Flags().Changed("region") {
			region, err := cmd.Flags().GetString("region")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			data[util.OCIStorageRegionField] = region
		}

		if cmd.Flags().Changed("namespace") {
			namespace, err := cmd.Flags().GetString("namespace")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if util.IsEmptyString(namespace) {
				delete(data, util.OCINamespaceField)
			} else {
				data[util.OCINamespaceField] = namespace
			}
		}

		wasIAM := isOCIIAMEnabled(data[util.UseOCIIAM])
		isIAM := wasIAM
		if cmd.Flags().Changed("use-oci-iam") {
			isIAM, err = cmd.Flags().GetBool("use-oci-iam")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}
		data[util.UseOCIIAM] = isIAM

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

		if isIAM {
			if !util.IsEmptyString(accessKeyID) || !util.IsEmptyString(secretAccessKey) ||
				!util.IsEmptyString(hostBase) {
				logrus.Fatalln(formatter.Colorize(
					"--access-key-id, --secret-access-key and --s3-host-base cannot be "+
						"used with OCI IAM based storage configurations\n", formatter.RedColor))
			}
			if ns, ok := data[util.OCINamespaceField]; !ok ||
				util.IsEmptyString(fmt.Sprintf("%v", ns)) {
				logrus.Fatalln(formatter.Colorize(
					"--namespace is required for OCI IAM based storage configurations\n",
					formatter.RedColor))
			}
			delete(data, util.OCIS3AccessKeyIDField)
			delete(data, util.OCIS3SecretAccessKeyField)
			delete(data, util.OCIS3HostBaseField)
		} else {
			if util.IsEmptyString(accessKeyID) && util.IsEmptyString(secretAccessKey) && wasIAM {
				accessKeyID, secretAccessKey, err = util.OCIS3CredentialsFromEnv()
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
			}
			if wasIAM && util.IsEmptyString(hostBase) {
				logrus.Fatalln(formatter.Colorize(
					"--s3-host-base is required when switching from OCI IAM "+
						"to S3-compatible credentials\n",
					formatter.RedColor))
			}
			if !util.IsEmptyString(accessKeyID) && !util.IsEmptyString(secretAccessKey) {
				data[util.OCIS3AccessKeyIDField] = accessKeyID
				data[util.OCIS3SecretAccessKeyField] = secretAccessKey
			}
			if !util.IsEmptyString(hostBase) {
				data[util.OCIS3HostBaseField] = hostBase
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
			util.FatalHTTPError(response, err, "Storage Configuration: OCI", "Update")
		}

		logrus.Infof("The storage configuration %s (%s) has been updated\n",
			formatter.Colorize(storageName, formatter.GreenColor), storageUUID)

		storageconfigurationutil.UpdateStorageConfigurationUtil(authAPI, storageName, storageUUID)

	},
}

// isOCIIAMEnabled handles USE_OCI_IAM stored either as a JSON boolean or a string.
func isOCIIAMEnabled(value interface{}) bool {
	switch v := value.(type) {
	case bool:
		return v
	case string:
		enabled, err := strconv.ParseBool(v)
		return err == nil && enabled
	}
	return false
}

func init() {
	updateOCIStorageConfigurationCmd.Flags().SortFlags = false

	updateOCIStorageConfigurationCmd.PersistentFlags().String("new-name", "",
		"[Optional] Update name of the storage configuration.")
	updateOCIStorageConfigurationCmd.Flags().String("region", "",
		"[Optional] Update OCI region for Object Storage, e.g. us-sanjose-1.")
	updateOCIStorageConfigurationCmd.Flags().Bool("use-oci-iam", false,
		"[Optional] Switch between OCI IAM and S3-compatible credentials. Switching to "+
			"S3-compatible credentials requires --s3-host-base and the key pair, from "+
			"--access-key-id and --secret-access-key or from OCI_S3_ACCESS_KEY_ID and "+
			"OCI_S3_SECRET_ACCESS_KEY. Switching to OCI IAM requires a namespace.")
	updateOCIStorageConfigurationCmd.Flags().String("namespace", "",
		fmt.Sprintf("[Optional] Update OCI Object Storage namespace. %s",
			formatter.Colorize(
				"Required for OCI IAM based storage configurations.",
				formatter.GreenColor)))
	updateOCIStorageConfigurationCmd.Flags().String("access-key-id", "",
		fmt.Sprintf("[Optional] Update OCI Customer Secret Key access key. %s "+
			"Can also be set using environment variable %s when switching off OCI IAM.",
			formatter.Colorize(
				"Provide with --secret-access-key for non IAM based storage configurations.",
				formatter.GreenColor),
			util.OCIS3AccessKeyIDField))
	updateOCIStorageConfigurationCmd.Flags().String("secret-access-key", "",
		fmt.Sprintf("[Optional] Update OCI Customer Secret Key secret. %s "+
			"Can also be set using environment variable %s when switching off OCI IAM.",
			formatter.Colorize(
				"Provide with --access-key-id for non IAM based storage configurations.",
				formatter.GreenColor),
			util.OCIS3SecretAccessKeyField))
	updateOCIStorageConfigurationCmd.Flags().String("s3-host-base", "",
		"[Optional] Update OCI S3-compatible API host base, e.g. "+
			"<namespace>.compat.objectstorage.<region>.oraclecloud.com.")
	updateOCIStorageConfigurationCmd.MarkFlagsRequiredTogether(
		"access-key-id", "secret-access-key")

}
