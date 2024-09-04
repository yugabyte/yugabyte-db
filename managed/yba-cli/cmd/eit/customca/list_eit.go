/*
 * Copyright (c) YugaByte, Inc.
 */

package customca

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var listCustomCAEITCmd = &cobra.Command{
	Use: "list",
	Short: "List Custom CA YugabyteDB Anywhere Encryption In Transit" +
		" (EIT) certificate configurations",
	Long: "List Custom CA YugabyteDB Anywhere Encryption In Transit" +
		" (EIT) certificate configurations",
	Run: func(cmd *cobra.Command, args []string) {
		providerutil.ListProviderUtil(cmd, "Custom CA", util.CustomCertHostPathCertificateType)
	},
}

func init() {
	listCustomCAEITCmd.Flags().SortFlags = false
}
