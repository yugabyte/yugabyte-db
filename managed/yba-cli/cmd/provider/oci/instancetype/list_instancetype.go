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
	Short:   "List instance types of a YugabyteDB Anywhere OCI provider",
	Long:    "List instance types of a YugabyteDB Anywhere OCI provider",
	Example: `yba provider oci instance-type list --name <provider-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		instancetypeutil.AddAndListInstanceTypeValidations(cmd, "list")
	},
	Run: func(cmd *cobra.Command, args []string) {
		instancetypeutil.ListInstanceTypeUtil(
			cmd,
			util.OCIProviderType,
			"OCI",
			"OCI",
		)
	},
}

func init() {
	listInstanceTypesCmd.Flags().SortFlags = false
}
