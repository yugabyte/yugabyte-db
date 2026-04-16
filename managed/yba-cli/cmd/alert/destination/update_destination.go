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

// updateDestinationAlertCmd represents the update alert command
var updateDestinationAlertCmd = &cobra.Command{
	Use:     "update",
	Aliases: []string{"edit"},
	Short:   "Update an alert destination in YugabyteDB Anywhere",
	Long:    "Update an alert destination in YugabyteDB Anywhere",
	Example: `yba alert destination update --name <alert-destination-name> \
     --set-default \
     --add-channel <alert-channel-1>,<alert-channel-2>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(name) {
			logrus.Fatal(
				formatter.Colorize(
					"No name specified to update alert destination\n",
					formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {

		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		var existingAlertDestination ybaclient.AlertDestination
		alertDestinations, response, err := authAPI.ListAlertDestinations().Execute()
		if err != nil {
			util.FatalHTTPError(
				response,
				err,
				"Alert Destination",
				"Update - List Alert Destinations",
			)
		}
		for _, alertDestination := range alertDestinations {
			if strings.EqualFold(alertDestination.GetName(), name) {
				existingAlertDestination = alertDestination
				break
			}
		}

		if util.IsEmptyString(existingAlertDestination.GetUuid()) {
			logrus.Fatal(
				formatter.Colorize(
					"Alert destination with name: "+name+" not found\n",
					formatter.RedColor))
		}

		newName, err := cmd.Flags().GetString("new-name")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(newName) {
			logrus.Debug("Updating alert destination name\n")
			existingAlertDestination.SetName(newName)
		}

		if cmd.Flags().Changed("set-default") {
			setDefault, err := cmd.Flags().GetBool("set-default")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			logrus.Debug("Updating alert destination is default\n")
			existingAlertDestination.SetDefaultDestination(setDefault)
		}

		addChannelString, err := cmd.Flags().GetString("add-channel")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		addChannelNames := strings.Split(addChannelString, ",")
		if len(addChannelString) == 0 {
			addChannelNames = make([]string, 0)
		}
		removeChannelString, err := cmd.Flags().GetString("remove-channel")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		removeChannelNames := strings.Split(removeChannelString, ",")
		if len(removeChannelString) == 0 {
			removeChannelNames = make([]string, 0)
		}
		channels, response, err := authAPI.ListAlertChannels().Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Alert Destination", "Update - List Alert Channels")
		}

		addChannelUUIDs := make([]string, 0)
		removeChannelUUIDs := make([]string, 0)

		logrus.Info("Add these: ", addChannelNames, "Remove these: ", removeChannelNames, "\n")
		for _, channel := range channels {
			for _, channelName := range addChannelNames {
				if strings.EqualFold(channel.GetName(), channelName) {
					addChannelUUIDs = append(addChannelUUIDs, channel.GetUuid())
				}
			}
			for _, channelName := range removeChannelNames {
				if strings.EqualFold(channel.GetName(), channelName) {
					removeChannelUUIDs = append(removeChannelUUIDs, channel.GetUuid())
				}
			}
		}

		if len(addChannelUUIDs) != len(addChannelNames) {
			logrus.Fatal(
				formatter.Colorize(
					"Alert channel(s) not found to add: "+strings.Join(addChannelNames, ", ")+".\n",
					formatter.RedColor))
		}

		if len(removeChannelUUIDs) != len(removeChannelNames) {
			logrus.Fatal(
				formatter.Colorize(
					"Alert channel(s) not found to remove: "+strings.Join(
						removeChannelNames,
						", ",
					)+".\n",
					formatter.RedColor,
				))
		}

		if len(removeChannelUUIDs) > 0 {
			removeSet := make(map[string]bool)
			for _, channelUUID := range removeChannelUUIDs {
				removeSet[channelUUID] = true
			}
			temp := make([]string, 0)
			existingChannels := existingAlertDestination.GetChannels()
			for _, existingChannelUUID := range existingChannels {
				if !removeSet[existingChannelUUID] {
					temp = append(temp, existingChannelUUID)
				}
			}
			logrus.Debug("Removing alert destination channels\n")
			existingAlertDestination.SetChannels(temp)
		}

		if len(addChannelUUIDs) > 0 {
			existingChannels := existingAlertDestination.GetChannels()
			channelSet := make(map[string]bool)
			for _, existing := range existingChannels {
				channelSet[existing] = true
			}

			// Add new channels only if they are not already present
			for _, channelUUID := range addChannelUUIDs {
				if !channelSet[channelUUID] {
					existingChannels = append(existingChannels, channelUUID)
					channelSet[channelUUID] = true // update the set so you don't add duplicates
				}
			}

			logrus.Debug("Adding alert destination channels\n")
			existingAlertDestination.SetChannels(existingChannels)
		}

		r, response, err := authAPI.UpdateAlertDestination(existingAlertDestination.GetUuid()).
			UpdateAlertDestinationRequest(ybaclient.AlertDestinationFormData{
				Name:               existingAlertDestination.GetName(),
				DefaultDestination: existingAlertDestination.GetDefaultDestination(),
				Channels:           existingAlertDestination.GetChannels(),
			}).
			Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Alert Destination", "Update")
		}

		alertDestination := util.CheckAndDereference(
			r,
			fmt.Sprintf("An error occurred while updating alert destination %s", name),
		)

		alertDestinationUUID := alertDestination.GetUuid()

		if util.IsEmptyString(alertDestinationUUID) {
			logrus.Fatal(formatter.Colorize(
				fmt.Sprintf(
					"An error occurred while adding alert destination %s\n",
					name),
				formatter.RedColor))
		}

		logrus.Infof(
			"Successfully updated alert destination %s (%s)\n",
			formatter.Colorize(name, formatter.GreenColor), alertDestinationUUID)

		alerts := make([]ybaclient.AlertDestination, 0)
		alerts = append(alerts, alertDestination)
		alertCtx := formatter.Context{
			Command: "update",
			Output:  os.Stdout,
			Format:  destination.NewAlertDestinationFormat(viper.GetString("output")),
		}

		destination.Write(alertCtx, alerts)

	},
}

func init() {
	updateDestinationAlertCmd.Flags().SortFlags = false

	updateDestinationAlertCmd.Flags().StringP("name", "n", "",
		"[Required] Name of alert destination to update. "+
			"Use single quotes ('') to provide values with special characters.")
	updateDestinationAlertCmd.MarkFlagRequired("name")
	updateDestinationAlertCmd.Flags().String("new-name", "",
		"[Optional] Update name of alert destination.")

	updateDestinationAlertCmd.Flags().Bool("set-default", false,
		"[Optional] Set this alert destination as default.")
	updateDestinationAlertCmd.Flags().String("add-channel", "",
		"[Optional] Add alert channels to destination. "+
			"Provide the comma separated channel names."+
			" Use single quotes ('') to provide values with special characters.")
	updateDestinationAlertCmd.Flags().String("remove-channel", "",
		"[Optional] Provide the comma separated channel names to be removed from destination."+
			" Use single quotes ('') to provide values with special characters.")

}
