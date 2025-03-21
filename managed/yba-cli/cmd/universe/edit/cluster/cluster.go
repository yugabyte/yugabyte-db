/*
 * Copyright (c) YugaByte, Inc.
 */

package cluster

import (
	"github.com/spf13/cobra"
)

// EditClusterCmd represents the universe command
var EditClusterCmd = &cobra.Command{
	Use:   "cluster",
	Short: "Edit clusters in a YugabyteDB Anywhere universe",
	Long:  "Edit clusters in a universe in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	EditClusterCmd.Flags().SortFlags = false

	EditClusterCmd.AddCommand(editPrimaryClusterCmd)
	EditClusterCmd.AddCommand(editReadReplicaClusterCmd)
	EditClusterCmd.AddCommand(resizeNodeCmd)

}
