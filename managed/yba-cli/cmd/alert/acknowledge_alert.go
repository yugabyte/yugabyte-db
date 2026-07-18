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

// acknowledgeAlertCmd represents the acknowledge alert command
var acknowledgeAlertCmd = &cobra.Command{
	Use:     "acknowledge",
	Short:   "Acknowledge YugabyteDB Anywhere alert",
	Long:    "Acknowledge alert in YugabyteDB Anywhere",
	Example: `yba alert acknowledge --uuid <alert-uuid>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		uuid, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(uuid) == 0 {
			logrus.Fatal(
				formatter.Colorize(
					"No UUID specified to acknowledge alert\n",
					formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {

		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		alertUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		rAcknowledge, response, err := authAPI.Acknowledge(alertUUID).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Alert", "Acknowledge")
		}

		r := util.CheckAndAppend(
			make([]ybaclient.Alert, 0),
			rAcknowledge,
			fmt.Sprintf("An error occurred while acknowledging alert: %s", alertUUID),
		)

		alertCtx := formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  alert.NewAlertFormat(viper.GetString("output")),
		}
		if len(r) < 1 {
			if util.IsOutputType(formatter.TableFormatKey) {
				logrus.Info("No alerts found\n")
			} else {
				logrus.Info("[]\n")
			}
			return
		}
		alert.Write(alertCtx, r)

	},
}

func init() {
	acknowledgeAlertCmd.Flags().SortFlags = false
	acknowledgeAlertCmd.Flags().StringP("uuid", "u", "", "[Required] UUID of alert to acknowledge.")
	acknowledgeAlertCmd.MarkFlagRequired("uuid")
}
