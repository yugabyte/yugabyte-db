/*
 * Copyright (c) YugaByte, Inc.
 */

package storageconfiguration

import (
	"fmt"
	"net/http"
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/storageconfiguration"
)

var listStorageConfigurationCmd = &cobra.Command{
	Use:   "list",
	Short: "List YugabyteDB Anywhere storage-configurations",
	Long:  "List YugabyteDB Anywhere storage-configurations",
	Run: func(cmd *cobra.Command, args []string) {
		authAPI, err := ybaAuthClient.NewAuthAPIClient()
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		authAPI.GetCustomerUUID()
		storageListRequest := authAPI.GetListOfCustomerConfig()

		var r []ybaclient.CustomerConfigUI
		var response *http.Response

		r, response, err = storageListRequest.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Storage Configuration", "List")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		storageConfigs := make([]ybaclient.CustomerConfigUI, 0)
		for _, s := range r {
			if strings.Compare(s.GetType(), util.StorageCustomerConfigType) == 0 {
				storageConfigs = append(storageConfigs, s)
			}
		}

		// filter by name and/or by storage-configurations code
		storageName, err := cmd.Flags().GetString("storage-config-name")
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
		storageCode, err := cmd.Flags().GetString("storage-config-code")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		var codes []string
		if storageCode != "" {
			codes = []string{strings.ToUpper(storageCode)}
		} else {
			codes = []string{"S3", "GCS", "AZ", "NFS"}
		}
		storageConfigsCode := make([]ybaclient.CustomerConfigUI, 0)
		for _, c := range codes {
			for _, s := range storageConfigs {
				if strings.Compare(s.GetName(), c) == 0 {
					storageConfigsCode = append(storageConfigsCode, s)
				}
			}
		}
		storageConfigs = storageConfigsCode

		storageCtx := formatter.Context{
			Output: os.Stdout,
			Format: storageconfiguration.NewStorageConfigFormat(viper.GetString("output")),
		}
		if len(storageConfigs) < 1 {
			fmt.Println("No storage configurations found")
			return
		}
		storageconfiguration.Write(storageCtx, storageConfigs)

	},
}

func init() {
	listStorageConfigurationCmd.Flags().SortFlags = false

	listStorageConfigurationCmd.Flags().StringP("storage-config-name", "n", "",
		"[Optional] Name of the storage configuration.")
	listStorageConfigurationCmd.Flags().StringP("storage-config-code", "c", "",
		"[Optional] Code of the storage configuration, defaults to list all"+
			" storage configurations. Allowed values: s3, gcs, nfs, az.")
}
