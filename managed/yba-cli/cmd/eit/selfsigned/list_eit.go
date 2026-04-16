/*
 * Copyright (c) YugabyteDB, Inc.
 */

package selfsigned

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/eit/eitutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var listSelfSignedEITCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short: "List Self Signed YugabyteDB Anywhere Encryption In Transit" +
		" (EIT) certificate configurations",
	Long: "List Self Signed YugabyteDB Anywhere Encryption In Transit" +
		" (EIT) certificate configurations",
	Example: `yba eit self-signed list`,
	Run: func(cmd *cobra.Command, args []string) {
		eitutil.ListEITUtil(cmd, "Self Signed", util.SelfSignedCertificateType)
	},
}

func init() {
	listSelfSignedEITCmd.Flags().SortFlags = false
}
