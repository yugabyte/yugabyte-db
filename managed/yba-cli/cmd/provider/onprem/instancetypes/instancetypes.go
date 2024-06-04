/*
 * Copyright (c) YugaByte, Inc.
 */

package instancetypes

import (
	"github.com/spf13/cobra"
)

// InstanceTypesCmd set of commands are used to perform operations on onprem instanceTypes
// in YugabyteDB Anywhere
var InstanceTypesCmd = &cobra.Command{
	Use:   "instance-types",
	Short: "Manage YugabyteDB Anywhere onprem instance types",
	Long:  "Manage YugabyteDB Anywhere on-premises instance types",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	InstanceTypesCmd.AddCommand(listInstanceTypesCmd)
	InstanceTypesCmd.AddCommand(describeInstanceTypesCmd)
	InstanceTypesCmd.AddCommand(removeInstanceTypesCmd)
	InstanceTypesCmd.AddCommand(addInstanceTypesCmd)
}
