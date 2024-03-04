/*
* Copyright (c) YugaByte, Inc.
 */

package schedule

import (
	"github.com/spf13/cobra"
)

// ScheduleCmd set of commands are used to perform schedule operations on universes
// in YugabyteDB Anywhere
var ScheduleCmd = &cobra.Command{
	Use:   "schedule",
	Short: "Manage YugabyteDB Anywhere universe backup schedules",
	Long:  "Manage YugabyteDB Anywhere universe backup schedules",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	ScheduleCmd.Flags().SortFlags = false
	ScheduleCmd.AddCommand(createBackupScheduleCmd)
}
