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

// createWebhookChannelAlertCmd represents the create alert command
var createWebhookChannelAlertCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add"},
	Short:   "Create a webhook alert channel in YugabyteDB Anywhere",
	Long:    "Create a webhook alert channel in YugabyteDB Anywhere",
	Example: `yba alert channel webhook create --name <alert-channel-name> \
   --auth-type <webhook-auth-type> --password <password> \
   --username <webhook-username> --webhook-url <webhook-url>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		channelutil.ValidateChannelUtil(cmd, "create")

		webhookURL, err := cmd.Flags().GetString("webhook-url")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(webhookURL) {
			logrus.Fatal(
				formatter.Colorize(
					"No webhook URL specified to create alert channel\n",
					formatter.RedColor))
		}

		authType, err := cmd.Flags().GetString("auth-type")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if strings.EqualFold(authType, util.BasicHttpAuthType) {
			username, err := cmd.Flags().GetString("username")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			password, err := cmd.Flags().GetString("password")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if util.IsEmptyString(username) || util.IsEmptyString(password) {
				logrus.Fatal(
					formatter.Colorize(
						"No username or password specified to create alert channel with basic authentication\n",
						formatter.RedColor,
					))
			}
		} else if strings.EqualFold(authType, util.TokenHttpAuthType) {
			tokenHeader, err := cmd.Flags().GetString("token-header")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			tokenValue, err := cmd.Flags().GetString("token-value")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if util.IsEmptyString(tokenValue) || util.IsEmptyString(tokenHeader) {
				logrus.Fatal(
					formatter.Colorize(
						"No token header or value specified to create alert channel with token authentication\n",
						formatter.RedColor))
			}
		}

	},
	Run: func(cmd *cobra.Command, args []string) {

		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		webhookURL, err := cmd.Flags().GetString("webhook-url")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		authType, err := cmd.Flags().GetString("auth-type")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		var username, password, tokenHeader, tokenValue string
		if strings.EqualFold(authType, util.BasicHttpAuthType) {
			username, err = cmd.Flags().GetString("username")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}

			password, err = cmd.Flags().GetString("password")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		} else if strings.EqualFold(authType, util.TokenHttpAuthType) {

			tokenHeader, err = cmd.Flags().GetString("token-header")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}

			tokenValue, err = cmd.Flags().GetString("token-value")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}

		sendResolved, err := cmd.Flags().GetBool("send-resolved-notifications")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		reqBody := util.AlertChannelFormData{
			Name: name,
			Params: util.AlertChannelParams{
				ChannelType:  util.GetStringPointer(util.WebhookAlertChannelType),
				Username:     username,
				WebhookUrl:   webhookURL,
				SendResolved: util.GetBoolPointer(sendResolved),
				HttpAuth: &util.HTTPAuthInformation{
					Type:        util.GetStringPointer(strings.ToUpper(authType)),
					Username:    util.GetStringPointer(username),
					Password:    util.GetStringPointer(password),
					TokenHeader: util.GetStringPointer(tokenHeader),
					TokenValue:  util.GetStringPointer(tokenValue),
				},
			},
		}

		channelutil.CreateChannelUtil(authAPI, "Alert Channel: Webhook", name, reqBody)

	},
}

func init() {
	createWebhookChannelAlertCmd.Flags().SortFlags = false

	createWebhookChannelAlertCmd.Flags().String("webhook-url", "",
		"[Required] Webhook webhook URL.")
	createWebhookChannelAlertCmd.MarkFlagRequired("webhook-url")
	createWebhookChannelAlertCmd.Flags().String("auth-type", "none",
		"[Optional] Authentication type for webhook. Allowed values: none, basic, token.")
	createWebhookChannelAlertCmd.Flags().String("username", "",
		fmt.Sprintf("[Optional] Username for authethicating webhook. %s.",
			formatter.Colorize("Required if auth type is basic", formatter.GreenColor)))
	createWebhookChannelAlertCmd.Flags().String("password", "",
		fmt.Sprintf("[Optional] Password for authethicating webhook. %s.",
			formatter.Colorize("Required if auth type is basic", formatter.GreenColor)))
	createWebhookChannelAlertCmd.MarkFlagsRequiredTogether("username", "password")
	createWebhookChannelAlertCmd.Flags().String("token-header", "",
		fmt.Sprintf("[Optional] Token header for authethicating webhook. %s.",
			formatter.Colorize("Required if auth type is token", formatter.GreenColor)))
	createWebhookChannelAlertCmd.Flags().String("token-value", "",
		fmt.Sprintf("[Optional] Token value for authethicating webhook. %s.",
			formatter.Colorize("Required if auth type is token", formatter.GreenColor)))
	createWebhookChannelAlertCmd.MarkFlagsRequiredTogether("token-header", "token-value")
	createWebhookChannelAlertCmd.Flags().Bool("send-resolved-notifications", true,
		"[Optional] Send resolved notifications for alert channel.")

}
