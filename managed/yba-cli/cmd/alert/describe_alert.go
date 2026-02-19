/*
* Copyright (c) YugabyteDB, Inc.
 */

package alert

import (
	"fmt"
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/alert"
)

// describeAlertCmd represents the describe alert command
var describeAlertCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe YugabyteDB Anywhere alert",
	Long:    "Describe alert in YugabyteDB Anywhere",
	Example: `yba alert describe --uuid <alert-uuid>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		uuid, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(uuid) == 0 {
			logrus.Fatal(
				formatter.Colorize(
					"No UUID specified to describe alert\n",
					formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {

		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		alertUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		alertRequest := authAPI.GetAlert(alertUUID)

		rAlert, response, err := alertRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Alert", "Describe")
		}

		r := util.CheckAndAppend(
			make([]ybaclient.Alert, 0),
			rAlert,
			fmt.Sprintf("No alert with UUID: %s found", alertUUID),
		)

		if len(r) > 0 && util.IsOutputType(formatter.TableFormatKey) {
			fullAlertContext := *alert.NewFullAlertContext()
			fullAlertContext.Output = os.Stdout
			fullAlertContext.Format = alert.NewFullAlertFormat(viper.GetString("output"))
			fullAlertContext.SetFullAlert(r[0])
			fullAlertContext.Write()
			return
		}

		if len(r) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No alerts with UUID: %s found\n", alertUUID),
					formatter.RedColor,
				))
		}

		alertCtx := formatter.Context{
			Command: "describe",
			Output:  os.Stdout,
			Format:  alert.NewAlertFormat(viper.GetString("output")),
		}
		alert.Write(alertCtx, r)

	},
}

func init() {
	describeAlertCmd.Flags().SortFlags = false
	describeAlertCmd.Flags().StringP("uuid", "u", "", "[Required] UUID of alert to describe.")
	describeAlertCmd.MarkFlagRequired("uuid")
}
