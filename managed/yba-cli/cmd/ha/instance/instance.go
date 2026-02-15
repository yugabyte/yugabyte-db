/*
 * Copyright (c) YugabyteDB, Inc.
 */

package instance

import (
	"github.com/spf13/cobra"
)

// InstanceCmd is the parent command for instance operations
var InstanceCmd = &cobra.Command{
	Use:   "instance",
	Short: "Manage HA instances",
	Long:  "Manage high availability instances for YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	InstanceCmd.AddCommand(deleteHAInstanceCmd)
	InstanceCmd.AddCommand(createHAInstanceCmd)
	InstanceCmd.AddCommand(getLocalHAInstanceCmd)
	InstanceCmd.AddCommand(promoteHAInstanceCmd)
}
