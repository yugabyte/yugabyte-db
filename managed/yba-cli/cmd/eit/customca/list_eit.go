/*
 * Copyright (c) YugabyteDB, Inc.
 */

package customca

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/eit/eitutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var listCustomCAEITCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short: "List Custom CA YugabyteDB Anywhere Encryption In Transit" +
		" (EIT) certificate configurations",
	Long: "List Custom CA YugabyteDB Anywhere Encryption In Transit" +
		" (EIT) certificate configurations",
	Example: `yba eit custom-ca list`,
	Run: func(cmd *cobra.Command, args []string) {
		eitutil.ListEITUtil(cmd, "Custom CA", util.CustomCertHostPathCertificateType)
	},
}

func init() {
	listCustomCAEITCmd.Flags().SortFlags = false
}
