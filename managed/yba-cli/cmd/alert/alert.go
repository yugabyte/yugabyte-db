/*
 * Copyright (c) YugaByte, Inc.
 */

package alert

import (
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/alert/channel"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/alert/configuration"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/alert/destination"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/alert/maintenancewindow"
)

// AlertCmd set of commands are used to perform operations on alerts
// in YugabyteDB Anywhere
var AlertCmd = &cobra.Command{
	Use:   "alert",
	Short: "Manage YugabyteDB Anywhere alerts",
	Long:  "Manage YugabyteDB Anywhere alerts",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	AlertCmd.PersistentFlags().SortFlags = false
	AlertCmd.Flags().SortFlags = false

	AlertCmd.AddCommand(listAlertCmd)
	AlertCmd.AddCommand(describeAlertCmd)
	AlertCmd.AddCommand(countAlertCmd)
	AlertCmd.AddCommand(acknowledgeAlertCmd)
	AlertCmd.AddCommand(controlAlertCmd)
	AlertCmd.AddCommand(configuration.ConfigurationAlertCmd)
	AlertCmd.AddCommand(destination.DestinationAlertCmd)
	AlertCmd.AddCommand(channel.ChannelAlertCmd)
	AlertCmd.AddCommand(maintenancewindow.MaintenanceWindowCmd)

}
