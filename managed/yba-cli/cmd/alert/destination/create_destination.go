/*
 * Copyright (c) YugabyteDB, Inc.
 */

package destination

import (
	"fmt"
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/alert/destination"
)

// createDestinationAlertCmd represents the create alert command
var createDestinationAlertCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add"},
	Short:   "Create an alert destination in YugabyteDB Anywhere",
	Long:    "Create an alert destination in YugabyteDB Anywhere",
	Example: `yba alert destination create --name <alert-destination-name> \
   --channels <alert-channel-1>,<alert-channel-2>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(name) {
			logrus.Fatal(
				formatter.Colorize(
					"No name specified to create alert destination\n",
					formatter.RedColor))
		}

	},
	Run: func(cmd *cobra.Command, args []string) {

		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		setDefault, err := cmd.Flags().GetBool("set-default")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		channelNames, err := cmd.Flags().GetString("channels")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		channelUUIDs := make([]string, 0)

		channels, response, err := authAPI.ListAlertChannels().Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Alert Destination", "Create - List Alert Channels")
		}

		if len(channels) == 0 {
			logrus.Fatal(
				formatter.Colorize(
					"No alert channels found in YugabyteDB Anywhere\n",
					formatter.RedColor))
		}

		for _, channelName := range strings.Split(channelNames, ",") {
			for _, channel := range channels {
				if strings.EqualFold(channel.GetName(), channelName) {
					channelUUIDs = append(channelUUIDs, channel.GetUuid())
				}
			}
		}

		if len(channelUUIDs) == 0 {
			logrus.Fatal(
				formatter.Colorize(
					"No alert channels found in YugabyteDB Anywhere with names: "+channelNames+"\n",
					formatter.RedColor))
		}

		if len(channelUUIDs) != len(strings.Split(channelNames, ",")) {
			logrus.Fatal(
				formatter.Colorize(
					fmt.Sprintf(
						"Could not find all alert channels with names: %s\n",
						channelNames),
					formatter.RedColor),
			)
		}

		reqBody := ybaclient.AlertDestinationFormData{
			Name:               name,
			DefaultDestination: setDefault,
			Channels:           channelUUIDs,
		}

		r, response, err := authAPI.CreateAlertDestination().
			CreateAlertDestinationRequest(reqBody).
			Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Alert Destination", "Create")
		}

		alertDestination := util.CheckAndDereference(
			r,
			fmt.Sprintf("An error occurred while adding alert destination %s", name),
		)

		alertConfigUUID := alertDestination.GetUuid()

		if util.IsEmptyString(alertConfigUUID) {
			logrus.Fatal(formatter.Colorize(
				fmt.Sprintf(
					"An error occurred while adding alert destination %s\n",
					name),
				formatter.RedColor))
		}

		logrus.Infof(
			"Successfully added alert destination %s (%s)\n",
			formatter.Colorize(name, formatter.GreenColor), alertConfigUUID)

		populateAlertChannels(authAPI, "Create")

		alerts := make([]ybaclient.AlertDestination, 0)
		alerts = append(alerts, alertDestination)
		alertCtx := formatter.Context{
			Command: "create",
			Output:  os.Stdout,
			Format:  destination.NewAlertDestinationFormat(viper.GetString("output")),
		}

		destination.Write(alertCtx, alerts)

	},
}

func init() {
	createDestinationAlertCmd.Flags().SortFlags = false

	createDestinationAlertCmd.Flags().StringP("name", "n", "",
		"[Required] Name of alert destination to create. "+
			"Use single quotes ('') to provide values with special characters.")
	createDestinationAlertCmd.MarkFlagRequired("name")
	createDestinationAlertCmd.Flags().Bool("set-default", false,
		"[Optional] Set this alert destination as default. (default false)")

	createDestinationAlertCmd.Flags().String("channels", "",
		"[Required] Comma separated list of alert channel names. "+
			"Use single quotes ('') to provide values with special characters. "+
			"Run \"yba alert channel list\" to check list of available channel names.")
	createDestinationAlertCmd.MarkFlagRequired("channels")
}
