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

// createPagerdutyChannelAlertCmd represents the create alert command
var createPagerdutyChannelAlertCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add"},
	Short:   "Create a PagerDuty alert channel in YugabyteDB Anywhere",
	Long:    "Create a PagerDuty alert channel in YugabyteDB Anywhere",
	Example: `yba alert channel pagerduty create --name <alert-channel-name> \
   --pagerduty-api-key <pagerduty-pagerduty-api-key> --routing-key <pagerduty-routing-key>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		channelutil.ValidateChannelUtil(cmd, "create")

		apiKey, err := cmd.Flags().GetString("pagerduty-api-key")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(apiKey) {
			logrus.Fatal(
				formatter.Colorize(
					"No API key specified to create PagerDuty alert channel\n",
					formatter.RedColor))
		}

		routingKey, err := cmd.Flags().GetString("routing-key")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(routingKey) {
			logrus.Fatal(
				formatter.Colorize(
					"No routing key specified to create PagerDuty alert channel\n",
					formatter.RedColor))
		}

	},
	Run: func(cmd *cobra.Command, args []string) {

		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		apiKey, err := cmd.Flags().GetString("pagerduty-api-key")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		routingKey, err := cmd.Flags().GetString("routing-key")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		reqBody := util.AlertChannelFormData{
			Name: name,
			Params: util.AlertChannelParams{
				ChannelType: util.GetStringPointer(util.PagerDutyAlertChannelType),
				ApiKey:      apiKey,
				RoutingKey:  routingKey,
			},
		}

		channelutil.CreateChannelUtil(authAPI, "Alert Channel: PagerDuty", name, reqBody)

	},
}

func init() {
	createPagerdutyChannelAlertCmd.Flags().SortFlags = false

	createPagerdutyChannelAlertCmd.Flags().String("pagerduty-api-key", "",
		"[Required] PagerDuty API key.")
	createPagerdutyChannelAlertCmd.MarkFlagRequired("pagerduty-api-key")

	createPagerdutyChannelAlertCmd.Flags().String("routing-key", "",
		"[Required] PagerDuty routing key.")
	createPagerdutyChannelAlertCmd.MarkFlagRequired("routing-key")

}
