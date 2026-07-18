/*
 * Copyright (c) YugabyteDB, Inc.
 */

package user

import (
	"github.com/spf13/cobra"
)

// UserCmd set of commands are used to perform operations on providers
// in YugabyteDB Anywhere
var UserCmd = &cobra.Command{
	Use:   "user",
	Short: "Manage YugabyteDB Anywhere users",
	Long:  "Manage YugabyteDB Anywhere users",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	UserCmd.AddCommand(listUserCmd)
	UserCmd.AddCommand(describeUserCmd)
	UserCmd.AddCommand(deleteUserCmd)
	UserCmd.AddCommand(createUserCmd)
	UserCmd.AddCommand(updateUserCmd)
	UserCmd.AddCommand(resetPasswordUserCmd)

}
