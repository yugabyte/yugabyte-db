/*
 * Copyright (c) YugaByte, Inc.
 */

package task

import (
	"github.com/spf13/cobra"
)

// TaskCmd set of commands are used to perform operations on tasks
// in YugabyteDB Anywhere
var TaskCmd = &cobra.Command{
	Use:   "task",
	Short: "Manage YugabyteDB Anywhere tasks",
	Long:  "Manage YugabyteDB Anywhere tasks",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	TaskCmd.AddCommand(retryTaskCmd)
	TaskCmd.AddCommand(listTaskCmd)
}
