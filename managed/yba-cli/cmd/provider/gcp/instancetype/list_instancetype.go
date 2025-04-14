/*
 * Copyright (c) YugaByte, Inc.
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
	Short:   "List instance types of a YugabyteDB Anywhere GCP provider",
	Long:    "List instance types of a YugabyteDB Anywhere GCP provider",
	Example: `yba provider gcp instance-type list --name <provider-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		instancetypeutil.AddAndListInstanceTypeValidations(cmd, "list")
	},
	Run: func(cmd *cobra.Command, args []string) {
		instancetypeutil.ListInstanceTypeUtil(
			cmd,
			util.GCPProviderType,
			"GCP",
			"GCP",
		)
	},
}

func init() {
	listInstanceTypesCmd.Flags().SortFlags = false

}
