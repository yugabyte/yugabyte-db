/*
 * Copyright (c) YugaByte, Inc.
 */

package instancetype

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil/instancetypeutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// removeInstanceTypesCmd represents the provider command
var removeInstanceTypesCmd = &cobra.Command{
	Use:     "remove",
	Aliases: []string{"delete", "rm"},
	Short:   "Delete instance type of a YugabyteDB Anywhere GCP provider",
	Long:    "Delete instance types of a YugabyteDB Anywhere GCP provider",
	Example: `yba provider gcp instance-type remove \
	--name <provider-name> --instance-type-name <instance-type-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		instancetypeutil.DeleteInstanceTypeValidations(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		instancetypeutil.RemoveInstanceTypeUtil(
			cmd,
			util.GCPProviderType,
			"GCP",
			"GCP",
		)
	},
}

func init() {
	removeInstanceTypesCmd.Flags().SortFlags = false

	removeInstanceTypesCmd.Flags().String("instance-type-name", "",
		"[Required] Instance type name.")
	removeInstanceTypesCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")

	removeInstanceTypesCmd.MarkFlagRequired("instance-type-name")
}
