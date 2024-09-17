/*
 * Copyright (c) YugaByte, Inc.
 */

package gcp

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/earutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var describeGCPEARCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a GCP YugabyteDB Anywhere Encryption In Transit (EAR) configuration",
	Long:    "Describe a GCP YugabyteDB Anywhere Encryption In Transit (EAR) configuration",
	PreRun: func(cmd *cobra.Command, args []string) {
		earutil.DescribeEARValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		earutil.DescribeEARUtil(cmd, "GCP", util.GCPEARType)

	},
}

func init() {
	describeGCPEARCmd.Flags().SortFlags = false
}
