/*
 * Copyright (c) YugabyteDB, Inc.
 */

package architecture

import (
	"github.com/spf13/cobra"
)

// ArchitectureReleaseCmd represents the release command to fetch metadata
var ArchitectureReleaseCmd = &cobra.Command{
	Use:     "architecture",
	Aliases: []string{"arch"},
	Short:   "Manage architectures for a version of YugabyteDB",
	Long:    "Manage architectures for a version of YugabyteDB",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	ArchitectureReleaseCmd.Flags().SortFlags = false

	ArchitectureReleaseCmd.AddCommand(addArchitectureReleaseCmd)
	ArchitectureReleaseCmd.AddCommand(editArchitectureReleaseCmd)

	ArchitectureReleaseCmd.PersistentFlags().SortFlags = false

	ArchitectureReleaseCmd.PersistentFlags().StringP("version", "v", "",
		"[Required] YugabyteDB version to be updated.")
	ArchitectureReleaseCmd.MarkPersistentFlagRequired("version")

}
