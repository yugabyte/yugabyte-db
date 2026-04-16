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

var describeDestinationAlertCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a YugabyteDB Anywhere alert destination",
	Long:    "Describe a YugabyteDB Anywhere alert destination",
	Example: `yba alert destination describe --name <alert-destination-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(name) {
			logrus.Fatal(
				formatter.Colorize(
					"No name specified to describe alert destination\n",
					formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()
		destinationName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		r, response, err := authAPI.ListAlertDestinations().Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Alert Destination", "Describe")
		}

		if !util.IsEmptyString(destinationName) {
			rNameList := make([]ybaclient.AlertDestination, 0)
			for _, alertDestination := range r {
				if strings.EqualFold(alertDestination.GetName(), destinationName) {
					rNameList = append(rNameList, alertDestination)
				}
			}

			r = rNameList
		}

		populateAlertChannels(authAPI, "Describe")

		if len(r) > 0 && util.IsOutputType(formatter.TableFormatKey) {
			fullDestinationContext := *destination.NewFullAlertDestinationContext()
			fullDestinationContext.Output = os.Stdout
			fullDestinationContext.Format = destination.NewFullAlertDestinationFormat(
				viper.GetString("output"),
			)
			fullDestinationContext.SetFullAlertDestination(r[0])
			fullDestinationContext.Write()
			return
		}

		if len(r) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No alert destinations with name: %s found\n", destinationName),
					formatter.RedColor,
				))
		}

		destinationCtx := formatter.Context{
			Command: "describe",
			Output:  os.Stdout,
			Format:  destination.NewAlertDestinationFormat(viper.GetString("output")),
		}
		destination.Write(destinationCtx, r)
	},
}

func init() {
	describeDestinationAlertCmd.Flags().SortFlags = false

	describeDestinationAlertCmd.Flags().
		StringP("name", "n", "", "[Required] Name of the alert destination.")
	describeDestinationAlertCmd.MarkFlagRequired("name")
}
