/*
 * Copyright (c) YugabyteDB, Inc.
 */

package destination

import (
	"fmt"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// deleteDestinationAlertCmd represents the delete alert command
var deleteDestinationAlertCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove", "rm"},
	Short:   "Delete YugabyteDB Anywhere alert destination",
	Long:    "Delete an alert destination in YugabyteDB Anywhere",
	Example: `yba alert destination delete --name <alert-destination-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(name) == 0 {
			logrus.Fatal(
				formatter.Colorize(
					"No name specified to delete alert destination\n",
					formatter.RedColor))
		}
		err = util.ConfirmCommand(
			fmt.Sprintf("Are you sure you want to delete %s: %s", "alert destination", name),
			viper.GetBool("force"))
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		destinationName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		alerts, response, err := authAPI.ListAlertDestinations().Execute()
		if err != nil {
			util.FatalHTTPError(
				response,
				err,
				"Alert Destination",
				"Delete - List Alert Destinations",
			)
		}

		if !util.IsEmptyString(destinationName) {
			rNameList := make([]ybaclient.AlertDestination, 0)
			for _, alertDestination := range alerts {
				if strings.EqualFold(alertDestination.GetName(), destinationName) {
					rNameList = append(rNameList, alertDestination)
				}
			}
			alerts = rNameList
		}

		if len(alerts) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No alert destinations with name: %s found\n", destinationName),
					formatter.RedColor),
			)
		}

		alertUUID := alerts[0].GetUuid()

		alertRequest := authAPI.DeleteAlertDestination(alertUUID)

		rDelete, response, err := alertRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Alert Destination", "Delete")
		}

		if rDelete.GetSuccess() {
			logrus.Info(fmt.Sprintf("The alert destination %s (%s) has been deleted\n",
				formatter.Colorize(destinationName, formatter.GreenColor), alertUUID))

		} else {
			logrus.Errorf(
				formatter.Colorize(
					fmt.Sprintf(
						"An error occurred while deleting alert destination %s (%s)\n",
						formatter.Colorize(destinationName, formatter.GreenColor), alertUUID),
					formatter.RedColor))
		}
	},
}

func init() {
	deleteDestinationAlertCmd.Flags().SortFlags = false
	deleteDestinationAlertCmd.Flags().StringP("name", "n", "",
		"[Required] Name of alert destination to delete. "+
			"Use single quotes ('') to provide values with special characters.")
	deleteDestinationAlertCmd.MarkFlagRequired("name")
	deleteDestinationAlertCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
