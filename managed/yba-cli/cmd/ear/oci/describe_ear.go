/*
 * Copyright (c) YugabyteDB, Inc.
 */

package oci

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/ear/earutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

var describeOCIEARCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe an OCI YugabyteDB Anywhere encryption at rest (EAR) configuration",
	Long:    "Describe an OCI YugabyteDB Anywhere encryption at rest (EAR) configuration",
	Example: `yba ear oci describe --name <config-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		earutil.DescribeEARValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		earutil.DescribeEARUtil(cmd, "OCI", util.OCIEARType)
	},
}

func init() {
	describeOCIEARCmd.Flags().SortFlags = false
}
