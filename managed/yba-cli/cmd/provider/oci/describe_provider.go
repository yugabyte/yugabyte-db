/*
 * Copyright (c) YugabyteDB, Inc.
 */

package oci

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var describeOCIProviderCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe an OCI YugabyteDB Anywhere provider",
	Long:    "Describe an OCI provider in YugabyteDB Anywhere",
	Example: `yba provider oci describe --name <provider-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		providerutil.DescribeProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		providerutil.DescribeProviderUtil(cmd, "OCI", util.OCIProviderType)
	},
}

func init() {
	describeOCIProviderCmd.Flags().SortFlags = false
}
