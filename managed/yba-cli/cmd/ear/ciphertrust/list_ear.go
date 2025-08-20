/*
 * Copyright (c) YugaByte, Inc.
 */

package ciphertrust

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/earutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var listCipherTrustEARCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List CipherTrust YugabyteDB Anywhere Encryption In Transit (EAR) configurations",
	Long:    "List CipherTrust YugabyteDB Anywhere Encryption In Transit (EAR) configurations",
	Example: `yba ear ciphertrust list`,
	Run: func(cmd *cobra.Command, args []string) {
		earutil.ListEARUtil(cmd, "CipherTrust", util.CipherTrustEARType)
	},
}

func init() {
	listCipherTrustEARCmd.Flags().SortFlags = false
}
