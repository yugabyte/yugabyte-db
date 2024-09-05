/*
 * Copyright (c) YugaByte, Inc.
 */

package customca

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/eit/eitutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// deleteCustomCAEITCmd represents the eit command
var deleteCustomCAEITCmd = &cobra.Command{
	Use:   "delete",
	Short: "Delete a YugabyteDB Anywhere Custom CA encryption in transit configuration",
	Long:  "Delete a Custom CA encryption in transit configuration in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		eitutil.DeleteEITValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		eitutil.DeleteEITUtil(cmd, "Custom CA", util.CustomCertHostPathCertificateType)
	},
}

func init() {
	deleteCustomCAEITCmd.Flags().SortFlags = false
	deleteCustomCAEITCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
