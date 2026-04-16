/*
 * Copyright (c) YugabyteDB, Inc.
 */

package keyinfo

import "github.com/spf13/cobra"

// KeyInfoCmd set of commands are used to perform operations on runtime
// configurations in YugabyteDB Anywhere
var KeyInfoCmd = &cobra.Command{
	Use:   "key-info",
	Short: "Get information about runtime configuration keys",
	Long:  "Get information about runtime configuration keys",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	KeyInfoCmd.Flags().SortFlags = false

	KeyInfoCmd.AddCommand(listKeyInfoCmd)
	KeyInfoCmd.AddCommand(describeKeyInfoCmd)
}
