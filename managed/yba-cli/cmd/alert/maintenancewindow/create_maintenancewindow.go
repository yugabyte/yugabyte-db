/*
 * Copyright (c) YugabyteDB, Inc.
 */

package maintenancewindow

import (
	"os"
	"strings"
	"time"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"

	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/maintenancewindow"
)

var createMaintenanceWindowCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add"},
	Short:   "Create a maintenance window to suppress alerts",
	Long:    "Create a maintenance window to suppress alerts",
	Run: func(cmd *cobra.Command, args []string) {

		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		description, err := cmd.Flags().GetString("description")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		startTimeString, err := cmd.Flags().GetString("start-time")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		startTime, err := time.Parse(time.RFC3339, startTimeString)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		endTimeString, err := cmd.Flags().GetString("end-time")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		endTime, err := time.Parse(time.RFC3339, endTimeString)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		alertTargetUUIDsString, err := cmd.Flags().GetString("alert-target-uuids")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		alertTargetUUIDs := make([]string, 0)
		alertUseAllTargets := false
		if !util.IsEmptyString(alertTargetUUIDsString) {
			alertTargetUUIDs = strings.Split(alertTargetUUIDsString, ",")
			alertUseAllTargets = false
		} else {
			alertUseAllTargets = true
		}

		healthTargetUUIDsString, err := cmd.Flags().GetString("health-check-universe-uuids")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		healthTargetUUIDs := make([]string, 0)
		healthUseAllTargets := false
		if !util.IsEmptyString(healthTargetUUIDsString) {
			healthTargetUUIDs = strings.Split(healthTargetUUIDsString, ",")
			healthUseAllTargets = false
		} else {
			healthUseAllTargets = true
		}

		req := ybaclient.MaintenanceWindow{
			Name:         name,
			Description:  description,
			StartTime:    startTime,
			EndTime:      endTime,
			CustomerUUID: authAPI.CustomerUUID,
			AlertConfigurationFilter: ybaclient.AlertConfigurationApiFilter{
				Target: &ybaclient.AlertConfigurationTarget{
					Uuids: alertTargetUUIDs,
					All:   util.GetBoolPointer(alertUseAllTargets),
				},
			},
			SuppressHealthCheckNotificationsConfig: &ybaclient.SuppressHealthCheckNotificationsConfig{
				UniverseUUIDSet:      healthTargetUUIDs,
				SuppressAllUniverses: util.GetBoolPointer(healthUseAllTargets),
			},
		}

		mw, response, err := authAPI.CreateMaintenanceWindow().
			CreateMaintenanceWindowRequest(req).
			Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Maintenance Window", "Create")
		}

		maintenanceWindow := util.CheckAndDereference(
			mw,
			"An error occurred while creating maintenance window",
		)

		r := []ybaclient.MaintenanceWindow{maintenanceWindow}

		logrus.Infof("The maintenance window %s (%s) has been created\n",
			formatter.Colorize(name, formatter.GreenColor), maintenanceWindow.GetUuid())

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

		maintenanceWindowCtx := formatter.Context{
			Command: "describe",
			Output:  os.Stdout,
			Format:  maintenancewindow.NewMaintenanceWindowFormat(viper.GetString("output")),
		}
		maintenancewindow.Write(maintenanceWindowCtx, r)

	},
}

func init() {
	createMaintenanceWindowCmd.Flags().SortFlags = false

	createMaintenanceWindowCmd.Flags().
		StringP("name", "n", "", "[Required] Name of the maintenance window.")
	createMaintenanceWindowCmd.MarkFlagRequired("name")
	createMaintenanceWindowCmd.Flags().
		String("description", "", "[Required] Description of the maintenance window.")
	createMaintenanceWindowCmd.MarkFlagRequired("description")
	createMaintenanceWindowCmd.Flags().
		String("start-time", "",
			"[Required] ISO 8601 (YYYY-MM-DDTHH:MM:SSZ) format for start time of the maintenance window.")
	createMaintenanceWindowCmd.MarkFlagRequired("start-time")
	createMaintenanceWindowCmd.Flags().
		String("end-time", "",
			"[Required] ISO 8601 (YYYY-MM-DDTHH:MM:SSZ) format for end time of the maintenance window.")
	createMaintenanceWindowCmd.MarkFlagRequired("end-time")
	createMaintenanceWindowCmd.Flags().String("alert-target-uuids", "",
		"[Optional] Comma separated list of universe UUIDs to suppress alerts. "+
			"If left empty, suppress alerts on all the universes (including future universes).")
	createMaintenanceWindowCmd.Flags().String("health-check-universe-uuids", "",
		"[Optional] Comma separated list of universe UUIDs to suppress health checks. "+
			"If left empty, suppress health check notifications on all the universes (including future universes).")
}
