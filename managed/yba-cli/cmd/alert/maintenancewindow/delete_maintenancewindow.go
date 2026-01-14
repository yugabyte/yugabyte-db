/*
 * Copyright (c) YugabyteDB, Inc.
 */

package maintenancewindow

import (
	"fmt"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

var deleteMaintenanceWindowCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove", "rm"},
	Short:   "Delete a YugabyteDB Anywhere maintenance window",
	Long:    "Delete a YugabyteDB Anywhere maintenance window",
	Example: `yba alert maintenance-window delete --uuid <maintenance-window-uuid>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		uuid, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(uuid) {
			logrus.Fatal(
				formatter.Colorize(
					"No uuid specified to delete maintenance window\n",
					formatter.RedColor))
		}
		err = util.ConfirmCommand(
			fmt.Sprintf("Are you sure you want to delete %s: %s", "maintenance window", uuid),
			viper.GetBool("force"))
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()
		maintenanceWindowUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		mw, response, err := authAPI.GetMaintenanceWindow(maintenanceWindowUUID).Execute()
		if err != nil {
			util.FatalHTTPError(
				response,
				err,
				"Maintenance Window",
				"Delete - Get Maintenance Window",
			)
		}

		if len(mw.GetCustomerUUID()) == 0 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf(
						"No maintenance windows with uuid: %s found\n",
						maintenanceWindowUUID,
					),
					formatter.RedColor,
				))
		}

		rDelete, response, err := authAPI.DeleteMaintenanceWindow(maintenanceWindowUUID).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Maintenance Window", "Delete")
		}

		if rDelete.GetSuccess() {
			logrus.Info(fmt.Sprintf("The maintenance window %s (%s) has been deleted\n",
				formatter.Colorize(mw.GetName(), formatter.GreenColor), maintenanceWindowUUID))

		} else {
			logrus.Errorf(
				formatter.Colorize(
					fmt.Sprintf(
						"An error occurred while deleting maintenance window %s (%s)\n",
						formatter.Colorize(mw.GetName(), formatter.GreenColor), maintenanceWindowUUID),
					formatter.RedColor))
		}
	},
}

func init() {
	deleteMaintenanceWindowCmd.Flags().SortFlags = false

	deleteMaintenanceWindowCmd.Flags().
		StringP("uuid", "u", "", "[Required] UUID of the maintenance window.")
	deleteMaintenanceWindowCmd.MarkFlagRequired("uuid")
	deleteMaintenanceWindowCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
