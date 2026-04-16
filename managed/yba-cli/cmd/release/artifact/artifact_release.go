/*
 * Copyright (c) YugabyteDB, Inc.
 */

package artifact

import (
	"github.com/spf13/cobra"
)

// ArtifactReleaseCmd represents the release command to fetch metadata
var ArtifactReleaseCmd = &cobra.Command{
	Use:     "artifact-create",
	Aliases: []string{"artifact"},
	Short:   "Fetch artifact metadata for a version of YugabyteDB",
	Long:    "Fetch artifact metadata for a version of YugabyteDB",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	ArtifactReleaseCmd.Flags().SortFlags = false

	ArtifactReleaseCmd.AddCommand(urlArtifactReleaseCmd)
	ArtifactReleaseCmd.AddCommand(fileArtifactReleaseCmd)
}
