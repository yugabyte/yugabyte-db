/*
 * Copyright (c) YugabyteDB, Inc.
 */

package instancetype

import (
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// SupportedStorageInstanceTypeCmd represents the supported-storage command
var SupportedStorageInstanceTypeCmd = &cobra.Command{
	Use:     "supported-storage",
	Aliases: []string{"supportedstorage", "supported-storage-types"},
	Short:   "List supported storage types for a YugabyteDB Anywhere OCI instance type",
	Long:    "List supported storage types for a YugabyteDB Anywhere OCI instance type",
	Example: `yba provider oci instance-type supported-storage`,
	Run: func(cmd *cobra.Command, args []string) {
		types := []string{"OCI_Balanced", "OCI_HigherPerformance", "OCI_LowerCost"}
		logrus.Infof(
			"Supported storage for YugabyteDB Anywhere OCI instance types: %v\n",
			util.GetPrintableList(types),
		)
	},
}
