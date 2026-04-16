/*
 * Copyright (c) YugabyteDB, Inc.
 */

package replication

import (
	"github.com/spf13/cobra"
)

// ReplicationCmd is the parent command for replication operations
var ReplicationCmd = &cobra.Command{
	Use:   "replication",
	Short: "Manage HA replication schedule",
	Long:  "Manage high availability replication schedule for YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	// Add subcommands to ReplicationCmd
	ReplicationCmd.AddCommand(startReplicationCmd)
	ReplicationCmd.AddCommand(stopReplicationCmd)
	ReplicationCmd.AddCommand(describeReplicationCmd)
}
