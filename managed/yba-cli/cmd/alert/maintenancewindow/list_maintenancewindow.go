/*
 * Copyright (c) YugabyteDB, Inc.
 */

package maintenancewindow

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
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/maintenancewindow"
)

var listMaintenanceWindowCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List YugabyteDB Anywhere maintenance windows",
	Long:    "List YugabyteDB Anywhere maintenance windows",
	Example: `yba alert maintenance-window list`,
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		maintenanceWindowListFilter := ybaclient.MaintenanceWindowApiFilter{}
		states, err := cmd.Flags().GetString("states")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(states) {
			statesList := strings.Split(states, ",")
			if len(statesList) > 0 {
				for i, state := range statesList {
					statesList[i] = strings.ToUpper(state)
				}
			}
			maintenanceWindowListFilter.SetStates(statesList)
		}

		uuids, err := cmd.Flags().GetString("uuids")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(uuids) {
			maintenanceWindowListFilter.SetUuids(strings.Split(uuids, ","))
		}

		maintenanceWindowListRequest := authAPI.ListOfMaintenanceWindows().
			ListMaintenanceWindowsRequest(maintenanceWindowListFilter)
		maintenanceWindowName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		r, response, err := maintenanceWindowListRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Maintenance Window", "List")
		}

		if len(maintenanceWindowName) > 0 {
			var windows []ybaclient.MaintenanceWindow
			for _, r := range r {
				if strings.Compare(r.GetName(), maintenanceWindowName) == 0 {
					windows = append(windows, r)
				}
			}
			r = windows
		}

		maintenanceWindowCtx := formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  maintenancewindow.NewMaintenanceWindowFormat(viper.GetString("output")),
		}
		if len(r) < 1 {
			if util.IsOutputType(formatter.TableFormatKey) {
				logrus.Info("No maintenance windows found\n")
			} else {
				logrus.Info("[]\n")
			}
			return
		}
		maintenancewindow.Write(maintenanceWindowCtx, r)

	},
}

func init() {
	listMaintenanceWindowCmd.Flags().SortFlags = false

	listMaintenanceWindowCmd.Flags().
		StringP("name", "n", "", "[Optional] Name of the maintenance window.")
	listMaintenanceWindowCmd.Flags().
		String("states", "",
			"[Optional] Comma separaed list of state of the maintenance window. Allowed values: active, pending, finished.")
	listMaintenanceWindowCmd.Flags().
		String("uuids", "", "[Optional] Comma separated list of UUIDs of the maintenance window.")
}
