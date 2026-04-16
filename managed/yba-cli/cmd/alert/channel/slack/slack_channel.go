/*
 * Copyright (c) YugabyteDB, Inc.
 */

package slack

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/alert/channel/channelutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// SlackChannelAlertCmd set of commands are used to perform operations on policies
// in YugabyteDB Anywhere
var SlackChannelAlertCmd = &cobra.Command{
	Use:     "slack",
	GroupID: "type",
	Short:   "Manage YugabyteDB Anywhere slack alert notification channels",
	Long:    "Manage YugabyteDB Anywhere slack alert notification channels ",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

// listSlackChannelAlertCmd represents the list alert command
var listSlackChannelAlertCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List YugabyteDB Anywhere slack alert channels",
	Long:    "List slack alert channels in YugabyteDB Anywhere",
	Example: `yba alert channel slack list`,
	Run: func(cmd *cobra.Command, args []string) {
		channelutil.ListChannelUtil(cmd, "Slack", util.SlackAlertChannelType)
	},
}

// deleteSlackChannelAlertCmd represents the delete alert command
var deleteSlackChannelAlertCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove", "rm"},
	Short:   "Delete YugabyteDB Anywhere slack alert channel",
	Long:    "Delete an slack alert channel in YugabyteDB Anywhere",
	Example: `yba alert channel slack delete --name <alert-channel-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		channelutil.ValidateDeleteChannelUtil(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		channelutil.DeleteChannelUtil(cmd, "Slack", util.SlackAlertChannelType)
	},
}

var describeSlackChannelAlertCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a YugabyteDB Anywhere slack alert channel",
	Long:    "Describe an slack alert channel in YugabyteDB Anywhere",
	Example: `yba alert channel slack describe --name <alert-channel-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		channelutil.ValidateChannelUtil(cmd, "describe")
	},
	Run: func(cmd *cobra.Command, args []string) {
		channelutil.DescribeChannelUtil(cmd, "Slack", util.SlackAlertChannelType)
	},
}

func init() {
	SlackChannelAlertCmd.PersistentFlags().SortFlags = false
	SlackChannelAlertCmd.Flags().SortFlags = false

	SlackChannelAlertCmd.AddCommand(listSlackChannelAlertCmd)
	SlackChannelAlertCmd.AddCommand(describeSlackChannelAlertCmd)
	SlackChannelAlertCmd.AddCommand(deleteSlackChannelAlertCmd)
	SlackChannelAlertCmd.AddCommand(createSlackChannelAlertCmd)
	SlackChannelAlertCmd.AddCommand(updateSlackChannelAlertCmd)

	SlackChannelAlertCmd.PersistentFlags().StringP("name", "n", "",
		fmt.Sprintf(
			"[Optional] The name of the alert channel for the operation. "+
				"Use single quotes ('') to provide values with special characters. %s.",
			formatter.Colorize(
				"Required for create, update, describe, delete",
				formatter.GreenColor,
			),
		))

	listSlackChannelAlertCmd.Flags().SortFlags = false

	deleteSlackChannelAlertCmd.Flags().SortFlags = false
	deleteSlackChannelAlertCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")

	describeSlackChannelAlertCmd.Flags().SortFlags = false

}
