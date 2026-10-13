/*
 * Copyright (c) YugabyteDB, Inc.
 */

package oci

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/earutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var refreshOCIEARCmd = &cobra.Command{
	Use:     "refresh",
	Short:   "Refresh an OCI YugabyteDB Anywhere encryption at rest (EAR) configuration",
	Long:    "Refresh an OCI YugabyteDB Anywhere encryption at rest (EAR) configuration",
	Example: `yba ear oci refresh --name <config-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		earutil.RefreshEARValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		earutil.RefreshEARUtil(cmd, "OCI", util.OCIEARType)
	},
}

func init() {
	refreshOCIEARCmd.Flags().SortFlags = false
}
