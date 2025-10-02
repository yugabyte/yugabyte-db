/*
 * Copyright (c) YugabyteDB, Inc.
 */

package key

import "github.com/spf13/cobra"

// KeyCmd set of commands are used to perform operations on runtime
// configurations in YugabyteDB Anywhere
var KeyCmd = &cobra.Command{
	Use:   "key",
	Short: "Manage YugabyteDB Anywhere runtime configuration scope keys",
	Long:  "Manage YugabyteDB Anywhere runtime configuration scope keys",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	KeyCmd.Flags().SortFlags = false

	KeyCmd.AddCommand(getKeyCmd)
	KeyCmd.AddCommand(setKeyCmd)
	KeyCmd.AddCommand(deleteKeyCmd)
}
