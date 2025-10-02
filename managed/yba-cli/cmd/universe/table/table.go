/*
 * Copyright (c) YugabyteDB, Inc.
 */

package table

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/table/namespace"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/table/tablespace"
)

// TableCmd set of commands are used to perform operations on universes
// in YugabyteDB Anywhere
var TableCmd = &cobra.Command{
	Use:   "table",
	Short: "Manage YugabyteDB Anywhere universe tables",
	Long:  "Manage YugabyteDB Anywhere universe tables",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	TableCmd.Flags().SortFlags = false

	TableCmd.AddCommand(listTableCmd)
	TableCmd.AddCommand(describeTableCmd)
	TableCmd.AddCommand(namespace.NamespaceCmd)
	TableCmd.AddCommand(tablespace.TablespaceCmd)

	TableCmd.PersistentFlags().StringP("name", "n", "",
		"[Required] The name of the universe for the corresponding table operations.")
	TableCmd.MarkPersistentFlagRequired("name")
}
