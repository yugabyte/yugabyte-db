/*
 * Copyright (c) YugabyteDB, Inc.
 */

package oci

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// deleteOCIProviderCmd represents the provider command
var deleteOCIProviderCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove", "rm"},
	Short:   "Delete an OCI YugabyteDB Anywhere provider",
	Long:    "Delete an OCI provider in YugabyteDB Anywhere",
	Example: `yba provider oci delete --name <provider-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		providerutil.DeleteProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		providerutil.DeleteProviderUtil(cmd, "OCI", util.OCIProviderType)
	},
}

func init() {
	deleteOCIProviderCmd.Flags().SortFlags = false
	deleteOCIProviderCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
