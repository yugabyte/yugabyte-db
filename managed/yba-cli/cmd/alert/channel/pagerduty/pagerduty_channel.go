/*
 * Copyright (c) YugaByte, Inc.
 */

package pagerduty

import (
	"fmt"

	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/alert/channel/channelutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// PagerdutyChannelAlertCmd set of commands are used to perform operations on policies
// in YugabyteDB Anywhere
var PagerdutyChannelAlertCmd = &cobra.Command{
	Use:     "pagerduty",
	Aliases: []string{"pager-duty"},
	GroupID: "type",
	Short:   "Manage YugabyteDB Anywhere PagerDuty alert notification channels",
	Long:    "Manage YugabyteDB Anywhere PagerDuty alert notification channels ",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

// listPagerdutyChannelAlertCmd represents the list alert command
var listPagerdutyChannelAlertCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List YugabyteDB Anywhere PagerDuty alert channels",
	Long:    "List PagerDuty alert channels in YugabyteDB Anywhere",
	Example: `yba alert channel pagerduty list`,
	Run: func(cmd *cobra.Command, args []string) {
		channelutil.ListChannelUtil(cmd, "PagerDuty", util.PagerDutyAlertChannelType)
	},
}

// deletePagerdutyChannelAlertCmd represents the delete alert command
var deletePagerdutyChannelAlertCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove", "rm"},
	Short:   "Delete YugabyteDB Anywhere PagerDuty alert channel",
	Long:    "Delete an PagerDuty alert channel in YugabyteDB Anywhere",
	Example: `yba alert channel pagerduty delete --name <alert-channel-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		channelutil.ValidateDeleteChannelUtil(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		channelutil.DeleteChannelUtil(cmd, "PagerDuty", util.PagerDutyAlertChannelType)
	},
}

var describePagerdutyChannelAlertCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a YugabyteDB Anywhere PagerDuty alert channel",
	Long:    "Describe a PagerDuty alert channel in YugabyteDB Anywhere",
	Example: `yba alert channel pagerduty describe --name <alert-channel-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		channelutil.ValidateChannelUtil(cmd, "describe")
	},
	Run: func(cmd *cobra.Command, args []string) {
		channelutil.DescribeChannelUtil(cmd, "PagerDuty", util.PagerDutyAlertChannelType)

	},
}

func init() {
	PagerdutyChannelAlertCmd.PersistentFlags().SortFlags = false
	PagerdutyChannelAlertCmd.Flags().SortFlags = false

	PagerdutyChannelAlertCmd.AddCommand(listPagerdutyChannelAlertCmd)
	PagerdutyChannelAlertCmd.AddCommand(describePagerdutyChannelAlertCmd)
	PagerdutyChannelAlertCmd.AddCommand(deletePagerdutyChannelAlertCmd)
	PagerdutyChannelAlertCmd.AddCommand(createPagerdutyChannelAlertCmd)
	PagerdutyChannelAlertCmd.AddCommand(updatePagerdutyChannelAlertCmd)

	PagerdutyChannelAlertCmd.PersistentFlags().StringP("name", "n", "",
		fmt.Sprintf(
			"[Optional] The name of the alert channel for the operation. "+
				"Use single quotes ('') to provide values with special characters. %s.",
			formatter.Colorize(
				"Required for create, update, describe, delete",
				formatter.GreenColor,
			),
		))

	listPagerdutyChannelAlertCmd.Flags().SortFlags = false

	deletePagerdutyChannelAlertCmd.Flags().SortFlags = false
	deletePagerdutyChannelAlertCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")

	describePagerdutyChannelAlertCmd.Flags().SortFlags = false

}
