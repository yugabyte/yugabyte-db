/*
 * Copyright (c) YugabyteDB, Inc.
 */

package pagerduty

import (
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/alert/channel/channelutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// updatePagerdutyChannelAlertCmd represents the ear command
var updatePagerdutyChannelAlertCmd = &cobra.Command{
	Use:     "update",
	Aliases: []string{"edit"},
	Short:   "Update a YugabyteDB Anywhere PagerDuty alert channel",
	Long:    "Update a PagerDuty alert channel in YugabyteDB Anywhere",
	Example: `yba alert channel pagerduty update --name <channel-name> \
   --new-name <new-channel-name> --pagerduty-api-key <pagerduty-api-key> \
   --routing-key <pagerduty-routing-key>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		channelutil.ValidateChannelUtil(cmd, "update")
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		callSite := "Alert Channel: PagerDuty"

		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		alerts := channelutil.ListAndFilterAlertChannels(
			authAPI,
			callSite,
			"Update - List Alert Channels",
			name,
			util.PagerDutyAlertChannelType,
		)

		if len(alerts) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					"No pagerduty alert channels with name: "+name+" found\n",
					formatter.RedColor,
				),
			)
		}

		alert := alerts[0]
		params := alert.GetParams()

		hasUpdates := false

		newName, err := cmd.Flags().GetString("new-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(newName) {
			hasUpdates = true
			logrus.Debug("Updating alert channel name\n")
			alert.SetName(newName)
		}

		apiKey, err := cmd.Flags().GetString("pagerduty-api-key")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(apiKey) {
			hasUpdates = true
			logrus.Debug("Updating alert channel PagerDuty API key\n")
			params.SetApiKey(apiKey)
		}

		routingKey, err := cmd.Flags().GetString("routing-key")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(routingKey) {
			hasUpdates = true
			logrus.Debug("Updating alert channel PagerDuty routing key\n")
			params.SetRoutingKey(routingKey)
		}

		alert.SetParams(params)

		requestBody := util.AlertChannelFormData{
			Name:             alert.GetName(),
			Params:           alert.GetParams(),
			AlertChannelUUID: alert.GetUuid(),
		}

		if hasUpdates {
			channelutil.UpdateChannelUtil(
				authAPI,
				util.PagerDutyAlertChannelType,
				name,
				alert.GetUuid(),
				requestBody)
			return
		}
		logrus.Fatal(formatter.Colorize("No fields found to update\n", formatter.RedColor))
	},
}

func init() {
	updatePagerdutyChannelAlertCmd.Flags().SortFlags = false

	updatePagerdutyChannelAlertCmd.Flags().String("new-name", "",
		"[Optional] Update name of the alert channel.")
	updatePagerdutyChannelAlertCmd.Flags().String("pagerduty-api-key", "",
		"[Optional] Update PagerDuty API key.")
	updatePagerdutyChannelAlertCmd.Flags().String("routing-key", "",
		"[Optional] Update PagerDuty routing key.")

}
