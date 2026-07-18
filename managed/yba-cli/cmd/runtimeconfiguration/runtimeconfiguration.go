/*
 * Copyright (c) YugabyteDB, Inc.
 */

package runtimeconfiguration

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/runtimeconfiguration/keyinfo"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/runtimeconfiguration/scope"
)

// RuntimeConfigurationCmd set of commands are used to perform operations on runtime
// configurations in YugabyteDB Anywhere
var RuntimeConfigurationCmd = &cobra.Command{
	Use:     "runtime-config",
	Aliases: []string{"runtime-configuration", "runtime"},
	Short:   "Manage YugabyteDB Anywhere runtime configurations",
	Long:    "Manage YugabyteDB Anywhere runtime configurations",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {

	RuntimeConfigurationCmd.AddCommand(keyinfo.KeyInfoCmd)
	RuntimeConfigurationCmd.AddCommand(scope.ScopeCmd)
}
