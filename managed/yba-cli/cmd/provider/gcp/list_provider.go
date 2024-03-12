/*
 * Copyright (c) YugaByte, Inc.
 */

package gcp

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var listGCPProviderCmd = &cobra.Command{
	Use:   "list",
	Short: "List GCP YugabyteDB Anywhere providers",
	Long:  "List GCP YugabyteDB Anywhere providers",
	Run: func(cmd *cobra.Command, args []string) {
		providerutil.ListProviderUtil(cmd, "GCP", util.GCPProviderType)
	},
}

func init() {
	listGCPProviderCmd.Flags().SortFlags = false
}
