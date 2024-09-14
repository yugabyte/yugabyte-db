/*
 * Copyright (c) YugaByte, Inc.
 */

package selfsigned

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var listSelfSignedEITCmd = &cobra.Command{
	Use: "list",
	Short: "List Self Signed YugabyteDB Anywhere Encryption In Transit" +
		" (EIT) certificate configurations",
	Long: "List Self Signed YugabyteDB Anywhere Encryption In Transit" +
		" (EIT) certificate configurations",
	Run: func(cmd *cobra.Command, args []string) {
		providerutil.ListProviderUtil(cmd, "Self Signed", util.SelfSignedCertificateType)
	},
}

func init() {
	listSelfSignedEITCmd.Flags().SortFlags = false
}
