/*
 * Copyright (c) YugaByte, Inc.
 */

package onprem

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var listOnpremProviderCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List On-premises YugabyteDB Anywhere providers",
	Long:    "List On-premises YugabyteDB Anywhere providers",
	Example: `yba provider onprem list`,
	Run: func(cmd *cobra.Command, args []string) {
		providerutil.ListProviderUtil(cmd, "On-premises", util.OnpremProviderType)
	},
}

func init() {
	listOnpremProviderCmd.Flags().SortFlags = false

}
