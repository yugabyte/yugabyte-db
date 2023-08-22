/*
 * Copyright (c) YugaByte, Inc.
 */

package releases

import (
	"github.com/spf13/cobra"
)

// ReleasesCmd set of commands are used to perform operations on releasess
// in YugabyteDB Anywhere
var ReleasesCmd = &cobra.Command{
	Use:   "yb-db-version",
	Short: "Manage YugabyteDB version release",
	Long:  "Manage YugabyteDB version release",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	ReleasesCmd.AddCommand(listReleasesCmd)
	//  ReleasesCmd.AddCommand(describeReleasesCmd)
	//  ReleasesCmd.AddCommand(deleteReleasesCmd)
	//  ReleasesCmd.AddCommand(createReleasesCmd)
}
