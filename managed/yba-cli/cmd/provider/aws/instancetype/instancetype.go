/*
 * Copyright (c) YugaByte, Inc.
 */

package instancetype

import (
	"github.com/spf13/cobra"
)

// InstanceTypesCmd set of commands are used to perform operations on aws instanceTypes
// in YugabyteDB Anywhere
var InstanceTypesCmd = &cobra.Command{
	Use:     "instance-type",
	Aliases: []string{"instance-types", "instancetypes", "instancetype"},
	Short:   "Manage YugabyteDB Anywhere AWS instance types",
	Long:    "Manage YugabyteDB Anywhere AWS instance types",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	InstanceTypesCmd.AddCommand(listInstanceTypesCmd)
	InstanceTypesCmd.AddCommand(describeInstanceTypesCmd)
	InstanceTypesCmd.AddCommand(removeInstanceTypesCmd)
	InstanceTypesCmd.AddCommand(addInstanceTypesCmd)
	InstanceTypesCmd.AddCommand(SupportedStorageInstanceTypeCmd)
}
