/*
 * Copyright (c) YugaByte, Inc.
 */

package selfsigned

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/eit/eitutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// deleteSelfSignedEITCmd represents the eit command
var deleteSelfSignedEITCmd = &cobra.Command{
	Use:   "delete",
	Short: "Delete a YugabyteDB Anywhere Self Signed encryption in transit configuration",
	Long:  "Delete a Self Signed encryption in transit configuration in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		eitutil.DeleteEITValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		eitutil.DeleteEITUtil(cmd, "Self Signed", util.SelfSignedCertificateType)
	},
}

func init() {
	deleteSelfSignedEITCmd.Flags().SortFlags = false
	deleteSelfSignedEITCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
