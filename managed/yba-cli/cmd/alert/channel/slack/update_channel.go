/*
 * Copyright (c) YugabyteDB, Inc.
 */

package slack

import (
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/alert/channel/channelutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// updateSlackChannelAlertCmd represents the ear command
var updateSlackChannelAlertCmd = &cobra.Command{
	Use:     "update",
	Aliases: []string{"edit"},
	Short:   "Update a YugabyteDB Anywhere Slack alert channel",
	Long:    "Update a Slack alert channel in YugabyteDB Anywhere",
	Example: `yba alert channel slack update --name <channel-name> \
  --new-name <new-channel-name> --username <slack-username> \
  --webhook-url <slack-webhook-url> --icon-url <slack-icon-url>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		channelutil.ValidateChannelUtil(cmd, "update")
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		callSite := "Alert Channel: Slack"

		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		alerts := channelutil.ListAndFilterAlertChannels(
			authAPI,
			callSite,
			"Update - List Alert Channels",
			name,
			util.SlackAlertChannelType,
		)

		if len(alerts) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					"No slack alert channels with name: "+name+" found\n",
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

		username, err := cmd.Flags().GetString("username")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(username) {
			hasUpdates = true
			logrus.Debug("Updating alert channel username\n")
			params.SetUsername(username)
		}

		webhookURL, err := cmd.Flags().GetString("webhook-url")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(webhookURL) {
			hasUpdates = true
			logrus.Debug("Updating alert channel webhook URL\n")
			params.SetWebhookUrl(webhookURL)
		}

		iconURL, err := cmd.Flags().GetString("icon-url")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(iconURL) {
			hasUpdates = true
			logrus.Debug("Updating alert channel icon URL\n")
			params.SetIconUrl(iconURL)
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
				util.SlackAlertChannelType,
				name,
				alert.GetUuid(),
				requestBody)
			return
		}
		logrus.Fatal(formatter.Colorize("No fields found to update\n", formatter.RedColor))
	},
}

func init() {
	updateSlackChannelAlertCmd.Flags().SortFlags = false

	updateSlackChannelAlertCmd.Flags().String("new-name", "",
		"[Optional] Update name of the alert channel.")
	updateSlackChannelAlertCmd.Flags().String("username", "",
		"[Optional] Update slack username.")
	updateSlackChannelAlertCmd.Flags().String("webhook-url", "",
		"[Optional] Update slack webhook URL.")
	updateSlackChannelAlertCmd.Flags().String("icon-url", "",
		"[Optional] Update slack icon URL.")

}
