/*
 * Copyright (c) YugaByte, Inc.
 */

package storageconfiguration

import (
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/storageconfiguration/storageconfigurationutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

var listStorageConfigurationCmd = &cobra.Command{
	Use:     "list",
	GroupID: "action",
	Short:   "List YugabyteDB Anywhere storage-configurations",
	Long:    "List YugabyteDB Anywhere storage-configurations",
	Run: func(cmd *cobra.Command, args []string) {
		storageCode, err := cmd.Flags().GetString("code")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		storageconfigurationutil.ListStorageConfigurationUtil(cmd, "", storageCode)

	},
}

func init() {
	listStorageConfigurationCmd.Flags().SortFlags = false

	listStorageConfigurationCmd.Flags().StringP("name", "n", "",
		"[Optional] Name of the storage configuration.")
	listStorageConfigurationCmd.Flags().StringP("code", "c", "",
		"[Optional] Code of the storage configuration, defaults to list all"+
			" storage configurations. Allowed values: s3, gcs, nfs, az.")
}
