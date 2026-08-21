/*
 * Copyright (c) YugabyteDB, Inc.
 */

package filecollection

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// FileCollectionCmd represents the file-collection command
var FileCollectionCmd = &cobra.Command{
	Use:     "file-collection",
	Aliases: []string{"fc", "filecollection"},
	Short:   "Collect, download, and delete diagnostic files from DB nodes",
	Long: "Collect, download, and delete diagnostic files from database nodes " +
		"in a YugabyteDB Anywhere universe",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	FileCollectionCmd.Flags().SortFlags = false

	FileCollectionCmd.PersistentFlags().SortFlags = false

	FileCollectionCmd.AddCommand(createFileCollectionCmd)
	FileCollectionCmd.AddCommand(downloadFileCollectionCmd)
	FileCollectionCmd.AddCommand(deleteFileCollectionCmd)

	FileCollectionCmd.PersistentFlags().StringP("name", "n", "",
		formatter.Colorize(
			"The name of the universe for the corresponding file collection operations. "+
				"Required for create, download, delete.",
			formatter.GreenColor,
		))

	FileCollectionCmd.PersistentFlags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
	FileCollectionCmd.PersistentFlags().BoolP("skip-validations", "s", false,
		"[Optional] Skip validations before running the CLI command.")
}
