/*
 * Copyright (c) YugabyteDB, Inc.
 */

package webhook

import (
	"fmt"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/alert/channel/channelutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// updateWebhookChannelAlertCmd represents the ear command
var updateWebhookChannelAlertCmd = &cobra.Command{
	Use:     "update",
	Aliases: []string{"edit"},
	Short:   "Update a YugabyteDB Anywhere Webhook alert channel",
	Long:    "Update a Webhook alert channel in YugabyteDB Anywhere",
	Example: `yba alert channel webhook update --name <channel-name> \
   --new-name <new-channel-name> --auth-type <webhook-auth-type> --username <webhook-username> \
   --password <password>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		channelutil.ValidateChannelUtil(cmd, "update")
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		callSite := "Alert Channel: Webhook"

		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		alerts := channelutil.ListAndFilterAlertChannels(
			authAPI,
			callSite,
			"Update - List Alert Channels",
			name,
			util.WebhookAlertChannelType,
		)

		if len(alerts) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					"No webhook alert channels with name: "+name+" found\n",
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

		webhookURL, err := cmd.Flags().GetString("webhook-url")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(webhookURL) {
			hasUpdates = true
			logrus.Debug("Updating alert channel webhook URL\n")
			params.SetWebhookUrl(webhookURL)
		}

		if cmd.Flags().Changed("send-resolved-notifications") {
			sendResolved, err := cmd.Flags().GetBool("send-resolved-notifications")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			hasUpdates = true
			logrus.Debug("Updating alert channel send resolved notifications\n")
			params.SetSendResolved(sendResolved)
		}

		httpAuth := params.GetHttpAuth()
		authType, err := cmd.Flags().GetString("auth-type")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(authType) {
			hasUpdates = true
			logrus.Debug("Updating alert channel auth type\n")
			httpAuth.SetType(strings.ToUpper(authType))
		}

		if strings.EqualFold(authType, util.BasicHttpAuthType) {
			username, err := cmd.Flags().GetString("username")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			password, err := cmd.Flags().GetString("password")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if !util.IsEmptyString(username) && !util.IsEmptyString(password) {
				hasUpdates = true
				logrus.Debug("Updating alert channel username and password\n")
				httpAuth.SetUsername(username)
				httpAuth.SetPassword(password)
			} else {
				logrus.Fatal(formatter.Colorize(
					"No username or password specified to update alert channel\n",
					formatter.RedColor))
			}
		} else if strings.EqualFold(authType, util.TokenHttpAuthType) {
			tokenHeader, err := cmd.Flags().GetString("token-header")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			tokenValue, err := cmd.Flags().GetString("token-value")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if !util.IsEmptyString(tokenHeader) && !util.IsEmptyString(tokenValue) {
				hasUpdates = true
				logrus.Debug("Updating alert channel token header and value\n")
				httpAuth.SetTokenHeader(tokenHeader)
				httpAuth.SetTokenValue(tokenValue)
			} else {
				logrus.Fatal(formatter.Colorize(
					"No token header or value specified to update alert channel\n",
					formatter.RedColor))
			}
		}
		params.SetHttpAuth(httpAuth)
		alert.SetParams(params)

		requestBody := util.AlertChannelFormData{
			Name:             alert.GetName(),
			Params:           alert.GetParams(),
			AlertChannelUUID: alert.GetUuid(),
		}

		if hasUpdates {
			channelutil.UpdateChannelUtil(
				authAPI,
				util.WebhookAlertChannelType,
				name,
				alert.GetUuid(),
				requestBody)
			return
		}
		logrus.Fatal(formatter.Colorize("No fields found to update\n", formatter.RedColor))
	},
}

func init() {
	updateWebhookChannelAlertCmd.Flags().SortFlags = false

	updateWebhookChannelAlertCmd.Flags().String("new-name", "",
		"[Optional] Update name of the alert channel.")
	updateWebhookChannelAlertCmd.Flags().String("webhook-url", "",
		"[Optional] Update webhook URL.")
	updateWebhookChannelAlertCmd.Flags().String("auth-type", "",
		"[Optional] Update authentication type for webhook. Allowed values: none, basic, token.")
	updateWebhookChannelAlertCmd.Flags().String("username", "",
		fmt.Sprintf("[Optional] Update username for authethicating webhook. %s.",
			formatter.Colorize("Required if auth type is basic", formatter.GreenColor)))
	updateWebhookChannelAlertCmd.Flags().String("password", "",
		fmt.Sprintf("[Optional] Update password for authethicating webhook. %s.",
			formatter.Colorize("Required if auth type is basic", formatter.GreenColor)))
	updateWebhookChannelAlertCmd.Flags().String("token-header", "",
		fmt.Sprintf("[Optional] Update token header for authethicating webhook. %s.",
			formatter.Colorize("Required if auth type is token", formatter.GreenColor)))
	updateWebhookChannelAlertCmd.Flags().String("token-value", "",
		fmt.Sprintf("[Optional] Update token value for authethicating webhook. %s.",
			formatter.Colorize("Required if auth type is token", formatter.GreenColor)))
	updateWebhookChannelAlertCmd.Flags().Bool("send-resolved-notifications", true,
		"[Optional] Send resolved notifications for alert channel.")

}
