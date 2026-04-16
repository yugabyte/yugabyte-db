/*
 * Copyright (c) YugabyteDB, Inc.
 */

package instancetype

import (
	"github.com/spf13/cobra"
)

// InstanceTypesCmd set of commands are used to perform operations on azure instanceTypes
// in YugabyteDB Anywhere
var InstanceTypesCmd = &cobra.Command{
	Use:     "instance-type",
	Aliases: []string{"instance-types", "instancetypes", "instancetype"},
	Short:   "Manage YugabyteDB Anywhere Azure instance types",
	Long:    "Manage YugabyteDB Anywhere Azure instance types",
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
