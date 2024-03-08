/*
 * Copyright (c) YugaByte, Inc.
 */

package aws

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// deleteAWSProviderCmd represents the provider command
var deleteAWSProviderCmd = &cobra.Command{
	Use:   "delete",
	Short: "Delete an AWS YugabyteDB Anywhere provider",
	Long:  "Delete an AWS provider in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		providerutil.DeleteProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		providerutil.DeleteProviderUtil(cmd, "AWS", util.AWSProviderType)
	},
}

func init() {
	deleteAWSProviderCmd.Flags().SortFlags = false
	deleteAWSProviderCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
