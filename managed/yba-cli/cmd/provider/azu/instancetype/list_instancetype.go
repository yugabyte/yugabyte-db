/*
 * Copyright (c) YugabyteDB, Inc.
 */

package instancetype

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil/instancetypeutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// listInstanceTypesCmd represents the provider command
var listInstanceTypesCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List instance types of a YugabyteDB Anywhere Azure provider",
	Long:    "List instance types of a YugabyteDB Anywhere Azure provider",
	Example: `yba provider azure instance-type list --name <provider-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		instancetypeutil.AddAndListInstanceTypeValidations(cmd, "list")
	},
	Run: func(cmd *cobra.Command, args []string) {
		instancetypeutil.ListInstanceTypeUtil(
			cmd,
			util.AzureProviderType,
			"Azure",
			"Azure",
		)
	},
}

func init() {
	listInstanceTypesCmd.Flags().SortFlags = false

}
