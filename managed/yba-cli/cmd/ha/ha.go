/*
 * Copyright (c) YugabyteDB, Inc.
 */

package ha

import (
	"github.com/spf13/cobra"
)

// HACmd set of commands are used to manage HA configuration
// in YugabyteDB Anywhere
var HACmd = &cobra.Command{
	Use:     "ha",
	Aliases: []string{"high-availability"},
	Short:   "Manage YugabyteDB Anywhere HA (High Availability) configuration",
	Long:    "Manage YugabyteDB Anywhere HA (High Availability) configuration",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	HACmd.PersistentFlags().SortFlags = false
	HACmd.Flags().SortFlags = false

	HACmd.AddCommand(describeHACmd)
	HACmd.AddCommand(deleteHACmd)
	HACmd.AddCommand(updateHACmd)
	HACmd.AddCommand(generateClusterKeyCmd)
}
