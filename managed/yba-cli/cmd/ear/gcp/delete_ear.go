/*
 * Copyright (c) YugaByte, Inc.
 */

package gcp

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/earutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// deleteGCPEARCmd represents the ear command
var deleteGCPEARCmd = &cobra.Command{
	Use:   "delete",
	Short: "Delete a YugabyteDB Anywhere GCP encryption at rest configuration",
	Long:  "Delete a GCP encryption at rest configuration in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		earutil.DeleteEARValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		earutil.DeleteEARUtil(cmd, "GCP", util.GCPEARType)
	},
}

func init() {
	deleteGCPEARCmd.Flags().SortFlags = false
	deleteGCPEARCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
