/*
 * Copyright (c) YugaByte, Inc.
 */

package xcluster

import (
	"github.com/spf13/cobra"
)

// XClusterCmd set of commands are used to perform operations on xClusters
// in YugabyteDB Anywhere
var XClusterCmd = &cobra.Command{
	Use:     "xcluster",
	Aliases: []string{"async-replication"},
	Short:   "Manage YugabyteDB Anywhere xClusters",
	Long:    "Manage YugabyteDB Anywhere xClusters (Asynchronous Replication)",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	XClusterCmd.AddCommand(listXClusterCmd)
	XClusterCmd.AddCommand(describeXClusterCmd)
	XClusterCmd.AddCommand(deleteXClusterCmd)
	XClusterCmd.AddCommand(syncXClusterCmd)
	XClusterCmd.AddCommand(restartXClusterCmd)
	XClusterCmd.AddCommand(pauseXClusterCmd)
	XClusterCmd.AddCommand(resumeXClusterCmd)

	// util.PreviewCommand(XClusterCmd, []*cobra.Command{
	// 	createXClusterCmd,
	// 	// updateXClusterCmd,
	// 	bootstrapTablesXClusterCmd,
	// })
}
