/*
* Copyright (c) YugabyteDB, Inc.
 */

package configuration

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

// deleteConfigurationAlertCmd represents the delete alert command
var deleteConfigurationAlertCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove", "rm"},
	Short:   "Delete YugabyteDB Anywhere alert policy",
	Long:    "Delete an alert policy in YugabyteDB Anywhere",
	Example: `yba alert policy delete --name <alert-configuration-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(name) == 0 {
			logrus.Fatal(
				formatter.Colorize(
					"No name specified to delete alert policy\n",
					formatter.RedColor))
		}
		err = util.ConfirmCommand(
			fmt.Sprintf("Are you sure you want to delete %s: %s", "alert policy", name),
			viper.GetBool("force"))
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {

		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		alertName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		alertConfigurationAPIFilter := ybaclient.AlertConfigurationApiFilter{
			Name: util.GetStringPointer(alertName),
		}

		direction := util.DescSortDirection
		var limit int32 = 10
		var offset int32 = 0
		sort := "createTime"

		alertAPIQuery := ybaclient.AlertConfigurationPagedApiQuery{
			Filter:    alertConfigurationAPIFilter,
			Direction: direction,
			Limit:     limit,
			Offset:    offset,
			SortBy:    sort,
		}

		alertListRequest := authAPI.PageAlertConfigurations().PageAlertConfigurationsRequest(
			alertAPIQuery,
		)
		alerts := make([]ybaclient.AlertConfiguration, 0)

		for {
			// Execute alert list request
			r, response, err := alertListRequest.Execute()
			if err != nil {
				util.FatalHTTPError(
					response,
					err,
					"Alert Policy",
					"Delete - List Alert Configurations",
				)
			}

			if len(r.GetEntities()) < 1 {
				break
			}

			for _, e := range r.GetEntities() {
				if strings.Compare(e.GetName(), alertName) == 0 {
					alerts = append(alerts, e)
				}
			}

			hasNext := r.GetHasNext()
			if !hasNext {
				break
			}

			offset += int32(len(r.GetEntities()))

			// Prepare next page request
			alertAPIQuery.Offset = offset
		}

		if len(alerts) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No alert policies with name: %s found\n", alertName),
					formatter.RedColor),
			)
		}

		alertUUID := alerts[0].GetUuid()

		alertRequest := authAPI.DeleteAlertConfiguration(alertUUID)

		rDelete, response, err := alertRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Alert Policy", "Delete")
		}

		if rDelete.GetSuccess() {
			logrus.Info(fmt.Sprintf("The alert policy %s (%s) has been deleted",
				formatter.Colorize(alertName, formatter.GreenColor), alertUUID))

		} else {
			logrus.Errorf(
				formatter.Colorize(
					fmt.Sprintf(
						"An error occurred while deleting alert policy %s (%s)\n",
						formatter.Colorize(alertName, formatter.GreenColor), alertUUID),
					formatter.RedColor))
		}
	},
}

func init() {
	deleteConfigurationAlertCmd.Flags().SortFlags = false
	deleteConfigurationAlertCmd.Flags().StringP("name", "n", "",
		"[Required] Name of alert policy to delete. "+
			"Use single quotes ('') to provide values with special characters.")
	deleteConfigurationAlertCmd.MarkFlagRequired("name")
	deleteConfigurationAlertCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
