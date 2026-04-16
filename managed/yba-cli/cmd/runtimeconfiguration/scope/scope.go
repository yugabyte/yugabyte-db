/*
 * Copyright (c) YugabyteDB, Inc.
 */

package scope

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/runtimeconfiguration/scope/key"
)

// ScopeCmd set of commands are used to perform operations on runtime
// configurations in YugabyteDB Anywhere
var ScopeCmd = &cobra.Command{
	Use:   "scope",
	Short: "Get information about runtime configuration scope",
	Long:  "Get information about runtime configuration scope",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	ScopeCmd.Flags().SortFlags = false

	ScopeCmd.AddCommand(listScopeCmd)
	ScopeCmd.AddCommand(describeScopeCmd)
	ScopeCmd.AddCommand(key.KeyCmd)
}
