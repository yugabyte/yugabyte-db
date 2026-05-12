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

// createSlackChannelAlertCmd represents the create alert command
var createSlackChannelAlertCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add"},
	Short:   "Create a slack alert channel in YugabyteDB Anywhere",
	Long:    "Create a slack alert channel in YugabyteDB Anywhere",
	Example: `yba alert channel slack create --name <alert-channel-name> \
  --username <slack-username> --webhook-url <slack-webhook-url>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		channelutil.ValidateChannelUtil(cmd, "create")

		username, err := cmd.Flags().GetString("username")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(username) {
			logrus.Fatal(
				formatter.Colorize(
					"No username specified to create alert channel\n",
					formatter.RedColor))
		}

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

	},
	Run: func(cmd *cobra.Command, args []string) {

		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		username, err := cmd.Flags().GetString("username")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		webhookURL, err := cmd.Flags().GetString("webhook-url")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		iconURL, err := cmd.Flags().GetString("icon-url")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		reqBody := util.AlertChannelFormData{
			Name: name,
			Params: util.AlertChannelParams{
				ChannelType: util.GetStringPointer(util.SlackAlertChannelType),
				Username:    username,
				WebhookUrl:  webhookURL,
				IconUrl:     util.GetStringPointer(iconURL),
			},
		}

		channelutil.CreateChannelUtil(authAPI, "Alert Channel: Slack", name, reqBody)

	},
}

func init() {
	createSlackChannelAlertCmd.Flags().SortFlags = false

	createSlackChannelAlertCmd.Flags().String("username", "",
		"[Required] Slack username.")
	createSlackChannelAlertCmd.MarkFlagRequired("username")
	createSlackChannelAlertCmd.Flags().String("webhook-url", "",
		"[Required] Slack webhook URL.")
	createSlackChannelAlertCmd.MarkFlagRequired("webhook-url")
	createSlackChannelAlertCmd.Flags().String("icon-url", "",
		"[Optional] Slack icon URL.")

}
