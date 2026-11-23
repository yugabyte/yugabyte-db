/*
 * Copyright (c) YugabyteDB, Inc.
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
	Use:     "update",
	Aliases: []string{"edit"},
	Short:   "Update an GCS YugabyteDB Anywhere storage configuration",
	Long:    "Update an GCS storage configuration in YugabyteDB Anywhere",
	Example: `yba storage-config gcs update --name <storage-configuration-name> \
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
				"Storage Configuration: GCS",
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

		wasIAM := util.IsTrueValue(data[util.UseGCPIAM])
		isIAM := wasIAM
		if cmd.Flags().Changed("use-gcp-iam") {
			isIAM, err = cmd.Flags().GetBool("use-gcp-iam")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			data[util.UseGCPIAM] = strconv.FormatBool(isIAM)
		}

		gcsFilePath, err := cmd.Flags().GetString("credentials-file-path")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		updateCreds, err := cmd.Flags().GetBool("update-credentials")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		err = applyGCSCreds(data, gcsFilePath,
			cmd.Flags().Changed("credentials-file-path"), updateCreds, wasIAM, isIAM)
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
			util.FatalHTTPError(response, err, "Storage Configuration: GCS", "Update")
		}

		logrus.Infof("The storage configuration %s (%s) has been updated\n",
			formatter.Colorize(storageName, formatter.GreenColor), storageUUID)

		storageconfigurationutil.UpdateStorageConfigurationUtil(authAPI, storageName, storageUUID)

	},
}

// applyGCSCreds writes the GCS credentials JSON into data. Without a file the stored credentials
// are kept, or read from the environment with --update-credentials or when switching off IAM.
// IAM drops the credentials.
func applyGCSCreds(
	data map[string]interface{},
	filePath string,
	fileFlagSet, updateCreds, wasIAM, isIAM bool,
) error {
	if isIAM {
		if fileFlagSet {
			return fmt.Errorf(
				"--credentials-file-path cannot be used with GCP IAM based storage configurations")
		}
		delete(data, util.GCSCredentialsJSON)
		return nil
	}
	var gcsCreds string
	var err error
	if !util.IsEmptyString(filePath) {
		gcsCreds, err = util.GcpGetCredentialsAsStringFromFilePath(filePath)
	} else if updateCreds || wasIAM {
		logrus.Infof("Reading GCS credentials from %s\n", util.GCPCredentialsEnv)
		gcsCreds, err = util.GcpGetCredentialsAsString()
	} else if fileFlagSet {
		return fmt.Errorf("--credentials-file-path must be non-empty")
	} else {
		return nil
	}
	if err != nil {
		return err
	}
	data[util.GCSCredentialsJSON] = gcsCreds
	return nil
}

func init() {
	updateGCSStorageConfigurationCmd.Flags().SortFlags = false

	// Flags needed for GCS
	updateGCSStorageConfigurationCmd.PersistentFlags().String("new-name", "",
		"[Optional] Update name of the storage configuration.")
	updateGCSStorageConfigurationCmd.Flags().Bool("update-credentials", false,
		"[Optional] Read new credentials from the file in GOOGLE_APPLICATION_CREDENTIALS"+
			" when --credentials-file-path is not given. (default false)")
	updateGCSStorageConfigurationCmd.Flags().String("credentials-file-path", "",
		fmt.Sprintf("GCS Credentials File Path. %s",
			formatter.Colorize(
				"Omit to keep the stored credentials. Required when switching off IAM, "+
					"unless GOOGLE_APPLICATION_CREDENTIALS is set.",
				formatter.GreenColor)))
	updateGCSStorageConfigurationCmd.Flags().Bool("use-gcp-iam", false,
		"[Optional] Use IAM Role from the YugabyteDB Anywhere Host. "+
			"Supported for Kubernetes GKE clusters with workload identity. Configuration "+
			"creation will fail on insufficient permissions on the host. (default false)")

}
