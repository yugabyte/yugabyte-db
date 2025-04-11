/*
 * Copyright (c) YugaByte, Inc.
 */

package instancetype

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil/instancetypeutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var describeInstanceTypesCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe instance type of a YugabyteDB Anywhere on-premises provider",
	Long:    "Describe instance types of a YugabyteDB Anywhere on-premises provider",
	Example: `yba provider onprem instance-type describe \
	--name <provider-name> --instance-type-name <instance-type-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		instancetypeutil.DescribeAndRemoveInstanceTypeValidations(cmd, "describe")
	},
	Run: func(cmd *cobra.Command, args []string) {
		instancetypeutil.DescribeInstanceTypeUtil(
			cmd,
			util.OnpremProviderType,
			"On-premises",
			"on-premises",
		)

	},
}

func init() {
	describeInstanceTypesCmd.Flags().SortFlags = false

	describeInstanceTypesCmd.Flags().String("instance-type-name", "",
		"[Required] Instance type name.")

	describeInstanceTypesCmd.MarkFlagRequired("instance-type-name")
}
