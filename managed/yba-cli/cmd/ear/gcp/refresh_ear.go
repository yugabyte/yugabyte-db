/*
 * Copyright (c) YugabyteDB, Inc.
 */

package gcp

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/earutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var refreshGCPEARCmd = &cobra.Command{
	Use:     "refresh",
	Short:   "Refresh a GCP YugabyteDB Anywhere Encryption In Transit (EAR) configuration",
	Long:    "Refresh a GCP YugabyteDB Anywhere Encryption In Transit (EAR) configuration",
	Example: `yba ear gcp refresh --name <config-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		earutil.RefreshEARValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		earutil.RefreshEARUtil(cmd, "GCP", util.GCPEARType)

	},
}

func init() {
	refreshGCPEARCmd.Flags().SortFlags = false
}
