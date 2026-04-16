/*
 * Copyright (c) YugabyteDB, Inc.
 */

package configuration

import (
	"fmt"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// testAlertConfigurationAlertCmd represents the testAlert alert command
var testAlertConfigurationAlertCmd = &cobra.Command{
	Use:     "test-alert",
	Aliases: []string{"testalert", "test"},
	Short:   "Send a test alert corresponding to YugabyteDB Anywhere alert policy",
	Long:    "Send a test alert corresponding to an alert policy in YugabyteDB Anywhere",
	Example: `yba alert policy test-alert --name <alert-configuration-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(name) == 0 {
			logrus.Fatal(
				formatter.Colorize(
					"No name specified to test alert policy\n",
					formatter.RedColor))
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
					"Test Alert - List Alert Configurations",
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
					fmt.Sprintf("No alert policy with name: %s found\n", alertName),
					formatter.RedColor),
			)
		}

		alertUUID := alerts[0].GetUuid()

		alertRequest := authAPI.SendTestAlert(alertUUID)

		rTest, response, err := alertRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Alert Policy", "Test Alert")
		}

		if rTest.GetSuccess() {
			logrus.Info(fmt.Sprintf("Successfully sent test alert for alert policy %s (%s)",
				formatter.Colorize(alertName, formatter.GreenColor), alertUUID))

		} else {
			logrus.Errorf(
				formatter.Colorize(
					fmt.Sprintf(
						"An error occurred while testing slerts for alert policy %s (%s)\n",
						formatter.Colorize(alertName, formatter.GreenColor), alertUUID),
					formatter.RedColor))
		}
	},
}

func init() {
	testAlertConfigurationAlertCmd.Flags().SortFlags = false
	testAlertConfigurationAlertCmd.Flags().StringP("name", "n", "",
		"[Required] Name of alert policy to test alerts. "+
			"Use single quotes ('') to provide values with special characters.")
	testAlertConfigurationAlertCmd.MarkFlagRequired("name")
}
