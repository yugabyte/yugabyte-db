/*
 * Copyright (c) YugabyteDB, Inc.
 */

package ciphertrust

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/earutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var refreshCipherTrustEARCmd = &cobra.Command{
	Use:     "refresh",
	Short:   "Refresh a CipherTrust YugabyteDB Anywhere Encryption In Transit (EAR) configuration",
	Long:    "Refresh a CipherTrust YugabyteDB Anywhere Encryption In Transit (EAR) configuration",
	Example: `yba ear ciphertrust refresh --name <config-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		earutil.RefreshEARValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		earutil.RefreshEARUtil(cmd, "CipherTrust", util.CipherTrustEARType)
	},
}

func init() {
	refreshCipherTrustEARCmd.Flags().SortFlags = false
}
