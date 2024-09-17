/*
 * Copyright (c) YugaByte, Inc.
 */

package aws

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/earutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var listAWSEARCmd = &cobra.Command{
	Use: "list",
	Short: "List AWS YugabyteDB Anywhere Encryption In Transit" +
		" (EAR) configurations",
	Long: "List AWS YugabyteDB Anywhere Encryption In Transit" +
		" (EAR) configurations",
	Run: func(cmd *cobra.Command, args []string) {
		earutil.ListEARUtil(cmd, "AWS", util.AWSEARType)
	},
}

func init() {
	listAWSEARCmd.Flags().SortFlags = false
}
