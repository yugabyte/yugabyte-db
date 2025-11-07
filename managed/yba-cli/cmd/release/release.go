/*
 * Copyright (c) YugabyteDB, Inc.
 */

package release

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/release/architecture"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/release/artifact"
)

// ReleaseCmd set of commands are used to perform operations on releasess
// in YugabyteDB Anywhere
var ReleaseCmd = &cobra.Command{
	Use:   "yb-db-version",
	Short: "Manage YugabyteDB versions",
	Long:  "Manage YugabyteDB versions",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	ReleaseCmd.AddCommand(listReleaseCmd)
	ReleaseCmd.AddCommand(describeReleaseCmd)
	ReleaseCmd.AddCommand(deleteReleaseCmd)
	ReleaseCmd.AddCommand(createReleaseCmd)
	ReleaseCmd.AddCommand(updateReleaseCmd)
	ReleaseCmd.AddCommand(artifact.ArtifactReleaseCmd)
	ReleaseCmd.AddCommand(architecture.ArchitectureReleaseCmd)
}
