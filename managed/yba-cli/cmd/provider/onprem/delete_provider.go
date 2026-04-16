/*
 * Copyright (c) YugabyteDB, Inc.
 */

package onprem

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// deleteOnpremProviderCmd represents the provider command
var deleteOnpremProviderCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove", "rm"},
	Short:   "Delete an On-premises YugabyteDB Anywhere provider",
	Long:    "Delete an On-premises provider in YugabyteDB Anywhere",
	Example: `yba provider onprem delete --name <provider-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		providerutil.DeleteProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		providerutil.DeleteProviderUtil(cmd, "On-premises", util.OnpremProviderType)
	},
}

func init() {
	deleteOnpremProviderCmd.Flags().SortFlags = false
	deleteOnpremProviderCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
