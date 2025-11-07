/*
 * Copyright (c) YugabyteDB, Inc.
 */

package edit

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/edit/cluster"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// EditUniverseCmd represents the universe command
var EditUniverseCmd = &cobra.Command{
	Use:     "edit",
	Short:   "Edit a YugabyteDB Anywhere universe",
	GroupID: "action",
	Long:    "Edit a universe in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	EditUniverseCmd.Flags().SortFlags = false
	EditUniverseCmd.PersistentFlags().SortFlags = false

	util.PreviewCommand(EditUniverseCmd, []*cobra.Command{
		editYSQLUniverseCmd,
		editYCQLUniverseCmd,
		cluster.EditClusterCmd,
	})

	EditUniverseCmd.PersistentFlags().StringP("name", "n", "",
		"[Required] The name of the universe to be edited.")
	EditUniverseCmd.MarkPersistentFlagRequired("name")

	EditUniverseCmd.PersistentFlags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
	EditUniverseCmd.PersistentFlags().BoolP("skip-validations", "s", false,
		"[Optional] Skip validations before running the CLI command.")
}
