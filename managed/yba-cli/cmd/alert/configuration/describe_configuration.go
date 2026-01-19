/*
* Copyright (c) YugabyteDB, Inc.
 */

package configuration

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
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/alert/configuration"
)

// describeConfigurationAlertCmd represents the describe alert command
var describeConfigurationAlertCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe YugabyteDB Anywhere alert policy",
	Long:    "Describe an alert policy in YugabyteDB Anywhere",
	Example: `yba alert policy describe --name <alert-configuration-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(name) == 0 {
			logrus.Fatal(
				formatter.Colorize(
					"No name specified to describe alert policy\n",
					formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {

		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		populateAlertDestinationAndTemplates(authAPI, "Describe")

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
					"Describe - List Alert Configurations",
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

		r := make([]ybaclient.AlertConfiguration, 0)
		r = append(r, alerts...)

		if len(r) > 0 && util.IsOutputType(formatter.TableFormatKey) {
			fullAlertContext := *configuration.NewFullAlertConfigurationContext()
			fullAlertContext.Output = os.Stdout
			fullAlertContext.Format = configuration.NewFullAlertConfigurationFormat(
				viper.GetString("output"),
			)
			fullAlertContext.SetFullAlertConfiguration(r[0])
			fullAlertContext.Write()
			return
		}

		if len(r) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No alert policies with name: %s found\n", alertName),
					formatter.RedColor,
				))
		}

		alertCtx := formatter.Context{
			Command: "describe",
			Output:  os.Stdout,
			Format: configuration.NewAlertConfigurationFormat(
				viper.GetString("output"),
			),
		}
		configuration.Write(alertCtx, r)

	},
}

func init() {
	describeConfigurationAlertCmd.Flags().SortFlags = false
	describeConfigurationAlertCmd.Flags().StringP("name", "n", "",
		"[Required] Name of alert policy to describe. "+
			"Use single quotes ('') to provide values with special characters.")
	describeConfigurationAlertCmd.MarkFlagRequired("name")
}
