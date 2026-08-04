/*
 * Copyright (c) YugabyteDB, Inc.
 */

package destination

import (
	"github.com/spf13/cobra"
)

// DestinationAlertCmd set of commands are used to perform operations on destinations
// in YugabyteDB Anywhere
var DestinationAlertCmd = &cobra.Command{
	Use:   "destination",
	Short: "Manage YugabyteDB Anywhere alert destinations",
	Long:  "Manage YugabyteDB Anywhere alert destinations (group of channels)",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	DestinationAlertCmd.PersistentFlags().SortFlags = false
	DestinationAlertCmd.Flags().SortFlags = false

	DestinationAlertCmd.AddCommand(listDestinationAlertCmd)
	DestinationAlertCmd.AddCommand(describeDestinationAlertCmd)
	DestinationAlertCmd.AddCommand(deleteDestinationAlertCmd)
	DestinationAlertCmd.AddCommand(createDestinationAlertCmd)
	DestinationAlertCmd.AddCommand(updateDestinationAlertCmd)

}
