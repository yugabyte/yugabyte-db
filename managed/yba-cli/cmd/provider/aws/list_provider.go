/*
 * Copyright (c) YugaByte, Inc.
 */

package aws

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/provider/providerutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var listAWSProviderCmd = &cobra.Command{
	Use:   "list",
	Short: "List AWS YugabyteDB Anywhere providers",
	Long:  "List AWS YugabyteDB Anywhere providers",
	Run: func(cmd *cobra.Command, args []string) {
		providerutil.ListProviderUtil(cmd, "AWS", util.AWSProviderType)
	},
}

func init() {
	listAWSProviderCmd.Flags().SortFlags = false
}
