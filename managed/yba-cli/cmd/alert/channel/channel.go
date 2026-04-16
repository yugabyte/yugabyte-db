/*
 * Copyright (c) YugabyteDB, Inc.
 */

package channel

import (
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/alert/channel/channelutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/alert/channel/email"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/alert/channel/pagerduty"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/alert/channel/slack"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/alert/channel/webhook"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// ChannelAlertCmd set of commands are used to perform operations on policies
// in YugabyteDB Anywhere
var ChannelAlertCmd = &cobra.Command{
	Use:     "channel",
	Aliases: []string{"notification-channel"},
	Short:   "Manage YugabyteDB Anywhere alert notification channels",
	Long:    "Manage YugabyteDB Anywhere alert notification channels ",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

// listChannelAlertCmd represents the list alert command
var listChannelAlertCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	GroupID: "action",
	Short:   "List YugabyteDB Anywhere alert channels",
	Long:    "List alert channels in YugabyteDB Anywhere",
	Example: `yba alert channel list`,
	Run: func(cmd *cobra.Command, args []string) {
		channelType, err := cmd.Flags().GetString("type")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		channelutil.ListChannelUtil(cmd, "", channelType)
	},
}

// deleteChannelAlertCmd represents the delete alert command
var deleteChannelAlertCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove", "rm"},
	GroupID: "action",
	Short:   "Delete YugabyteDB Anywhere alert channel",
	Long:    "Delete an alert channel in YugabyteDB Anywhere",
	Example: `yba alert channel delete --name <alert-channel-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		channelutil.ValidateDeleteChannelUtil(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		channelutil.DeleteChannelUtil(cmd, "", "")
	},
}

var describeChannelAlertCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	GroupID: "action",
	Short:   "Describe a YugabyteDB Anywhere alert channel",
	Long:    "Describe an alert channel in YugabyteDB Anywhere",
	Example: `yba alert channel describe --name <alert-channel-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		channelutil.ValidateChannelUtil(cmd, "describe")
	},
	Run: func(cmd *cobra.Command, args []string) {
		channelutil.DescribeChannelUtil(cmd, "", "")

	},
}

func init() {
	ChannelAlertCmd.PersistentFlags().SortFlags = false
	ChannelAlertCmd.Flags().SortFlags = false

	ChannelAlertCmd.AddCommand(listChannelAlertCmd)
	ChannelAlertCmd.AddCommand(describeChannelAlertCmd)
	ChannelAlertCmd.AddCommand(deleteChannelAlertCmd)

	ChannelAlertCmd.AddCommand(email.EmailChannelAlertCmd)
	ChannelAlertCmd.AddCommand(webhook.WebhookChannelAlertCmd)
	ChannelAlertCmd.AddCommand(pagerduty.PagerdutyChannelAlertCmd)
	ChannelAlertCmd.AddCommand(slack.SlackChannelAlertCmd)

	ChannelAlertCmd.AddGroup(&cobra.Group{
		ID:    "action",
		Title: "Action Commands",
	})
	ChannelAlertCmd.AddGroup(&cobra.Group{
		ID:    "type",
		Title: "Alert Channel Type Commands",
	})

	deleteChannelAlertCmd.Flags().SortFlags = false
	deleteChannelAlertCmd.Flags().StringP("name", "n", "",
		"[Required] Name of alert channel to delete. "+
			"Use single quotes ('') to provide values with special characters.")
	deleteChannelAlertCmd.MarkFlagRequired("name")
	deleteChannelAlertCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")

	describeChannelAlertCmd.Flags().SortFlags = false
	describeChannelAlertCmd.Flags().StringP("name", "n", "",
		"[Required] The name of the alert channel to be described.")
	describeChannelAlertCmd.MarkFlagRequired("name")

	listChannelAlertCmd.Flags().SortFlags = false
	listChannelAlertCmd.Flags().StringP("name", "n", "",
		"[Optional] Name of the alert channel to filter.")
	listChannelAlertCmd.Flags().String("type", "",
		"[Optional] Type of the alert channel to filter. Allowed values: email, webhook, pagerduty, slack.")

}
