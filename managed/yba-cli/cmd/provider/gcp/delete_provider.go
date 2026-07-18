/*
 * Copyright (c) YugabyteDB, Inc.
 */

package gcp

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// deleteGCPProviderCmd represents the provider command
var deleteGCPProviderCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove", "rm"},
	Short:   "Delete a GCP YugabyteDB Anywhere provider",
	Long:    "Delete a GCP provider in YugabyteDB Anywhere",
	Example: `yba provider gcp delete --name <provider-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		providerutil.DeleteProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		providerutil.DeleteProviderUtil(cmd, "GCP", util.GCPProviderType)
	},
}

func init() {
	deleteGCPProviderCmd.Flags().SortFlags = false
	deleteGCPProviderCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
