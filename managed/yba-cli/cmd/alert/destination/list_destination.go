/*
 * Copyright (c) YugabyteDB, Inc.
 */

package destination

import (
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

var listDestinationAlertCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List YugabyteDB Anywhere alert destinations",
	Long:    "List YugabyteDB Anywhere alert destinations",
	Example: `yba alert destination list`,
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		destinationListRequest := authAPI.ListAlertDestinations()
		// filter by name and/or by destination code
		destinationName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		r, response, err := destinationListRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Alert Destination", "List")
		}

		if len(destinationName) > 0 {
			var filteredDestinations []ybaclient.AlertDestination
			for _, r := range r {
				if strings.Compare(r.GetName(), destinationName) == 0 {
					filteredDestinations = append(filteredDestinations, r)
				}
			}
			r = filteredDestinations
		}

		destinationCtx := formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  destination.NewAlertDestinationFormat(viper.GetString("output")),
		}
		if len(r) < 1 {
			if util.IsOutputType(formatter.TableFormatKey) {
				logrus.Info("No alert destinations found\n")
			} else {
				logrus.Info("[]\n")
			}
			return
		}
		destination.Write(destinationCtx, r)

	},
}

func init() {
	listDestinationAlertCmd.Flags().SortFlags = false

	listDestinationAlertCmd.Flags().
		StringP("name", "n", "", "[Optional] Name of the alert destination.")
}
