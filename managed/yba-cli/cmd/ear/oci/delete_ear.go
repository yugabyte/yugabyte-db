/*
 * Copyright (c) YugabyteDB, Inc.
 */

package oci

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/earutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// deleteOCIEARCmd represents the ear command
var deleteOCIEARCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove", "rm"},
	Short:   "Delete a YugabyteDB Anywhere OCI encryption at rest configuration",
	Long:    "Delete an OCI encryption at rest configuration in YugabyteDB Anywhere",
	Example: `yba ear oci delete --name <config-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		earutil.DeleteEARValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		earutil.DeleteEARUtil(cmd, "OCI", util.OCIEARType)
	},
}

func init() {
	deleteOCIEARCmd.Flags().SortFlags = false
	deleteOCIEARCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
