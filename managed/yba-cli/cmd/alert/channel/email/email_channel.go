/*
 * Copyright (c) YugaByte, Inc.
 */

package email

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/alert/channel/channelutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// EmailChannelAlertCmd set of commands are used to perform operations on policies
// in YugabyteDB Anywhere
var EmailChannelAlertCmd = &cobra.Command{
	Use:     "email",
	GroupID: "type",
	Short:   "Manage YugabyteDB Anywhere email alert notification channels",
	Long:    "Manage YugabyteDB Anywhere email alert notification channels ",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

// listEmailChannelAlertCmd represents the list alert command
var listEmailChannelAlertCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List YugabyteDB Anywhere email alert channels",
	Long:    "List email alert channels in YugabyteDB Anywhere",
	Example: `yba alert channel email list`,
	Run: func(cmd *cobra.Command, args []string) {
		channelutil.ListChannelUtil(cmd, "Email", util.EmailAlertChannelType)
	},
}

// deleteEmailChannelAlertCmd represents the delete alert command
var deleteEmailChannelAlertCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove", "rm"},
	Short:   "Delete YugabyteDB Anywhere email alert channel",
	Long:    "Delete an email alert channel in YugabyteDB Anywhere",
	Example: `yba alert channel email delete --name <alert-channel-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		channelutil.ValidateDeleteChannelUtil(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		channelutil.DeleteChannelUtil(cmd, "Email", util.EmailAlertChannelType)
	},
}

var describeEmailChannelAlertCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a YugabyteDB Anywhere email alert channel",
	Long:    "Describe an email alert channel in YugabyteDB Anywhere",
	Example: `yba alert channel email describe --name <alert-channel-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		channelutil.ValidateChannelUtil(cmd, "describe")
	},
	Run: func(cmd *cobra.Command, args []string) {
		channelutil.DescribeChannelUtil(cmd, "Email", util.EmailAlertChannelType)

	},
}

func init() {
	EmailChannelAlertCmd.PersistentFlags().SortFlags = false
	EmailChannelAlertCmd.Flags().SortFlags = false

	EmailChannelAlertCmd.AddCommand(listEmailChannelAlertCmd)
	EmailChannelAlertCmd.AddCommand(describeEmailChannelAlertCmd)
	EmailChannelAlertCmd.AddCommand(deleteEmailChannelAlertCmd)
	EmailChannelAlertCmd.AddCommand(createEmailChannelAlertCmd)
	EmailChannelAlertCmd.AddCommand(updateEmailChannelAlertCmd)

	EmailChannelAlertCmd.PersistentFlags().StringP("name", "n", "",
		fmt.Sprintf(
			"[Optional] The name of the alert channel for the operation. "+
				"Use single quotes ('') to provide values with special characters. %s.",
			formatter.Colorize(
				"Required for create, update, describe, delete",
				formatter.GreenColor,
			),
		))

	listEmailChannelAlertCmd.Flags().SortFlags = false

	deleteEmailChannelAlertCmd.Flags().SortFlags = false
	deleteEmailChannelAlertCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")

	describeEmailChannelAlertCmd.Flags().SortFlags = false
}
