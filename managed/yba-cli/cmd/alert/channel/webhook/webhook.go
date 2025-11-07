/*
 * Copyright (c) YugabyteDB, Inc.
 */

package webhook

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/alert/channel/channelutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// WebhookChannelAlertCmd set of commands are used to perform operations on policies
// in YugabyteDB Anywhere
var WebhookChannelAlertCmd = &cobra.Command{
	Use:     "webhook",
	GroupID: "type",
	Short:   "Manage YugabyteDB Anywhere webhook alert notification channels",
	Long:    "Manage YugabyteDB Anywhere webhook alert notification channels ",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

// listWebhookChannelAlertCmd represents the list alert command
var listWebhookChannelAlertCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List YugabyteDB Anywhere webhook alert channels",
	Long:    "List webhook alert channels in YugabyteDB Anywhere",
	Example: `yba alert channel webhook list`,
	Run: func(cmd *cobra.Command, args []string) {
		channelutil.ListChannelUtil(cmd, "Webhook", util.WebhookAlertChannelType)
	},
}

// deleteWebhookChannelAlertCmd represents the delete alert command
var deleteWebhookChannelAlertCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove", "rm"},
	Short:   "Delete YugabyteDB Anywhere webhook alert channel",
	Long:    "Delete an webhook alert channel in YugabyteDB Anywhere",
	Example: `yba alert channel webhook delete --name <alert-channel-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		channelutil.ValidateDeleteChannelUtil(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		channelutil.DeleteChannelUtil(cmd, "Webhook", util.WebhookAlertChannelType)
	},
}

var describeWebhookChannelAlertCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a YugabyteDB Anywhere webhook alert channel",
	Long:    "Describe an webhook alert channel in YugabyteDB Anywhere",
	Example: `yba alert channel webhook describe --name <alert-channel-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		channelutil.ValidateChannelUtil(cmd, "describe")
	},
	Run: func(cmd *cobra.Command, args []string) {
		channelutil.DescribeChannelUtil(cmd, "Webhook", util.WebhookAlertChannelType)

	},
}

func init() {
	WebhookChannelAlertCmd.PersistentFlags().SortFlags = false
	WebhookChannelAlertCmd.Flags().SortFlags = false

	WebhookChannelAlertCmd.AddCommand(listWebhookChannelAlertCmd)
	WebhookChannelAlertCmd.AddCommand(describeWebhookChannelAlertCmd)
	WebhookChannelAlertCmd.AddCommand(deleteWebhookChannelAlertCmd)
	WebhookChannelAlertCmd.AddCommand(createWebhookChannelAlertCmd)
	WebhookChannelAlertCmd.AddCommand(updateWebhookChannelAlertCmd)

	WebhookChannelAlertCmd.PersistentFlags().StringP("name", "n", "",
		fmt.Sprintf(
			"[Optional] The name of the alert channel for the operation. "+
				"Use single quotes ('') to provide values with special characters. %s.",
			formatter.Colorize(
				"Required for create, update, describe, delete",
				formatter.GreenColor,
			),
		))

	listWebhookChannelAlertCmd.Flags().SortFlags = false

	deleteWebhookChannelAlertCmd.Flags().SortFlags = false
	deleteWebhookChannelAlertCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")

	describeWebhookChannelAlertCmd.Flags().SortFlags = false
}
