/*
 * Copyright (c) YugaByte, Inc.
 */

package supportbundle

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// SupportBundleUniverseCmd represents the universe command
var SupportBundleUniverseCmd = &cobra.Command{
	Use:     "support-bundle",
	Aliases: []string{"sb", "supportbundle"},
	Short:   "Support bundle operations on a YugabyteDB Anywhere universe",
	Long:    "Support bundle operations on a universe in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	SupportBundleUniverseCmd.Flags().SortFlags = false

	SupportBundleUniverseCmd.PersistentFlags().SortFlags = false

	SupportBundleUniverseCmd.AddCommand(listSupportBundleComponentsUniverseCmd)
	SupportBundleUniverseCmd.AddCommand(createSupportBundleUniverseCmd)
	SupportBundleUniverseCmd.AddCommand(deleteSupportBundleUniverseCmd)
	SupportBundleUniverseCmd.AddCommand(downloadSupportBundleUniverseCmd)
	SupportBundleUniverseCmd.AddCommand(describeSupportBundleUniverseCmd)
	SupportBundleUniverseCmd.AddCommand(listSupportBundleUniverseCmd)

	SupportBundleUniverseCmd.PersistentFlags().StringP("name", "n", "",
		"[Optional] The name of the universe for support bundle operations. "+
			formatter.Colorize(
				"Required for create, delete, list, describe, download.",
				formatter.GreenColor,
			))

	SupportBundleUniverseCmd.PersistentFlags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
	SupportBundleUniverseCmd.PersistentFlags().BoolP("skip-validations", "s", false,
		"[Optional] Skip validations before running the CLI command.")
}
