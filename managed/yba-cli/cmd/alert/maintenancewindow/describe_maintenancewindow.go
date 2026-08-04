/*
 * Copyright (c) YugabyteDB, Inc.
 */

package maintenancewindow

import (
	"fmt"
	"net/http"
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/maintenancewindow"
)

var describeMaintenanceWindowCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a YugabyteDB Anywhere maintenance window",
	Long:    "Describe a YugabyteDB Anywhere maintenance window",
	Example: `yba alert maintenance-window describe --uuid <maintenance-window-uuid>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		uuid, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(uuid) {
			logrus.Fatal(
				formatter.Colorize(
					"No uuid specified to describe maintenance window\n",
					formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		var err error
		var response *http.Response

		maintenancewindow.Universes, response, err = authAPI.ListUniverses().Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Maintenance Window", "Describe - List Universes")
		}

		maintenanceWindowUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		mw, response, err := authAPI.GetMaintenanceWindow(maintenanceWindowUUID).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Maintenance Window", "Describe")
		}

		maintenanceWindow := util.CheckAndDereference(
			mw,
			fmt.Sprintf(
				"An error occurred while getting maintenance window with uuid: %s",
				maintenanceWindowUUID,
			),
		)

		r := []ybaclient.MaintenanceWindow{maintenanceWindow}

		if len(r) > 0 && util.IsOutputType(formatter.TableFormatKey) {
			fullMaintenanceWindowContext := *maintenancewindow.NewFullMaintenanceWindowContext()
			fullMaintenanceWindowContext.Output = os.Stdout
			fullMaintenanceWindowContext.Format = maintenancewindow.NewFullMaintenanceWindowFormat(
				viper.GetString("output"),
			)
			fullMaintenanceWindowContext.SetFullMaintenanceWindow(r[0])
			fullMaintenanceWindowContext.Write()
			return
		}

		if len(r) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf(
						"No maintenance windows with uuid: %s found\n",
						maintenanceWindowUUID,
					),
					formatter.RedColor,
				))
		}

		maintenanceWindowCtx := formatter.Context{
			Command: "describe",
			Output:  os.Stdout,
			Format:  maintenancewindow.NewMaintenanceWindowFormat(viper.GetString("output")),
		}
		maintenancewindow.Write(maintenanceWindowCtx, r)
	},
}

func init() {
	describeMaintenanceWindowCmd.Flags().SortFlags = false

	describeMaintenanceWindowCmd.Flags().
		StringP("uuid", "u", "", "[Required] UUID of the maintenance window.")
	describeMaintenanceWindowCmd.MarkFlagRequired("uuid")
}
