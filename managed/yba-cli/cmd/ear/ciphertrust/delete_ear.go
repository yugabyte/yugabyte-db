/*
 * Copyright (c) YugaByte, Inc.
 */

package ciphertrust

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/earutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// deleteCipherTrustEARCmd represents the ear command
var deleteCipherTrustEARCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove", "rm"},
	Short:   "Delete a YugabyteDB Anywhere CipherTrust encryption at rest configuration",
	Long:    "Delete a CipherTrust encryption at rest configuration in YugabyteDB Anywhere",
	Example: `yba ear ciphertrust delete --name <config-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		earutil.DeleteEARValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		earutil.DeleteEARUtil(cmd, "CipherTrust", util.CipherTrustEARType)
	},
}

func init() {
	deleteCipherTrustEARCmd.Flags().SortFlags = false
	deleteCipherTrustEARCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
