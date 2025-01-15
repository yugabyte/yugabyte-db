/*
 * Copyright (c) YugaByte, Inc.
 */

package namespace

import (
	"github.com/spf13/cobra"
)

// NamespaceCmd set of commands are used to perform operations on universes
// in YugabyteDB Anywhere
var NamespaceCmd = &cobra.Command{
	Use:   "namespace",
	Short: "Manage YugabyteDB Anywhere universe table namespaces",
	Long:  "Manage YugabyteDB Anywhere universe table namespaces",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	NamespaceCmd.AddCommand(listNamespaceCmd)
}
