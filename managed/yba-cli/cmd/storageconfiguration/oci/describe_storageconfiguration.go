/*
 * Copyright (c) YugabyteDB, Inc.
 */

package oci

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/storageconfiguration/storageconfigurationutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var describeOCIStorageConfigurationCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe an OCI YugabyteDB Anywhere storage configuration",
	Long:    "Describe an OCI storage configuration in YugabyteDB Anywhere",
	Example: `yba storage-config oci describe --name <storage-configuration-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		storageconfigurationutil.DescribeStorageConfigurationValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		storageconfigurationutil.DescribeStorageConfigurationUtil(cmd, util.OCIStorageConfigType)
	},
}

func init() {
	describeOCIStorageConfigurationCmd.Flags().SortFlags = false

}
