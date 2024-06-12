/*
* Copyright (c) YugaByte, Inc.
 */

package restore

import (
	"github.com/spf13/cobra"
)

// RestoreCmd set of commands are used to perform restore operations on universes
// in YugabyteDB Anywhere
var RestoreCmd = &cobra.Command{
	Use:   "restore",
	Short: "Manage YugabyteDB Anywhere universe backup restores",
	Long:  "Manage YugabyteDB Anywhere universe backup restores",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	RestoreCmd.Flags().SortFlags = false

	RestoreCmd.AddCommand(createBackupRestoreCmd)
	RestoreCmd.AddCommand(listRestoreCmd)
}
