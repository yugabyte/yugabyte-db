/*
 * Copyright (c) YugabyteDB, Inc.
 */

package gcp

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var describeGCPProviderCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a GCP YugabyteDB Anywhere provider",
	Long:    "Describe a GCP provider in YugabyteDB Anywhere",
	Example: `yba provider gcp describe --name <provider-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		providerutil.DescribeProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		providerutil.DescribeProviderUtil(cmd, "GCP", util.GCPProviderType)
	},
}

func init() {
	describeGCPProviderCmd.Flags().SortFlags = false
}
