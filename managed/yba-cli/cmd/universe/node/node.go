/*
 * Copyright (c) YugaByte, Inc.
 */

package node

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// NodeCmd set of commands are used to perform operations on nodes
// in YugabyteDB Anywhere
var NodeCmd = &cobra.Command{
	Use:   "node",
	Short: "Manage YugabyteDB Anywhere universe nodes",
	Long: "Manage YugabyteDB Anywhere universe nodes. " +
		"Operations allowed for AWS, Azure, GCP and On-premises universes.",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	NodeCmd.Flags().SortFlags = false
	// stop -> remove -> release
	NodeCmd.AddCommand(addNodeCmd)
	NodeCmd.AddCommand(startNodeCmd)
	NodeCmd.AddCommand(rebootNodeCmd)
	NodeCmd.AddCommand(listNodeCmd)
	NodeCmd.AddCommand(stopNodeCmd)
	NodeCmd.AddCommand(removeNodeCmd)
	NodeCmd.AddCommand(reprovisionNodeCmd)
	NodeCmd.AddCommand(releaseNodeCmd)

	NodeCmd.PersistentFlags().StringP("name", "n", "",
		"[Required] The name of the universe for the corresponding node operations.")
	NodeCmd.MarkPersistentFlagRequired("name")
	NodeCmd.PersistentFlags().String("node-name", "",
		fmt.Sprintf(
			"[Optional] The name of the universe node for the corresponding node operations. %s",
			formatter.Colorize(
				"Required for add, reboot, release, remove, reprovision, start, stop.",
				formatter.GreenColor)))

}
