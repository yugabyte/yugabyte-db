/*
 * Copyright (c) YugaByte, Inc.
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

var updateMaintenanceWindowCmd = &cobra.Command{
	Use:     "update",
	Aliases: []string{"edit"},
	Short:   "Update a maintenance window to suppress alerts",
	Long:    "Update a maintenance window to suppress alerts",
	PreRun: func(cmd *cobra.Command, args []string) {
		uuid, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(uuid)) == 0 {
			logrus.Fatal(
				formatter.Colorize(
					"No uuid specified to update maintenance window\n",
					formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {

		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		uuid, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		existingMW, response, err := authAPI.GetMaintenanceWindow(uuid).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response, err, "Maintenance Window", "Update - Get Maintenance Window")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(name)) != 0 {
			logrus.Info("Updating maintenance window name\n")
			existingMW.SetName(name)
		}

		description, err := cmd.Flags().GetString("description")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(description)) != 0 {
			logrus.Info("Updating maintenance window description\n")
			existingMW.SetDescription(description)
		}

		startTimeString, err := cmd.Flags().GetString("start-time")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(startTimeString)) != 0 {
			startTime, err := time.Parse(time.RFC3339, startTimeString)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			logrus.Info("Updating maintenance window start time\n")
			existingMW.SetStartTime(startTime)
		}

		endTimeString, err := cmd.Flags().GetString("end-time")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if len(strings.TrimSpace(endTimeString)) != 0 {
			endTime, err := time.Parse(time.RFC3339, endTimeString)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if endTime.Before(existingMW.GetStartTime()) {
				logrus.Fatalf(
					formatter.Colorize(
						"End time cannot be before start time\n",
						formatter.RedColor,
					),
				)
			}
			logrus.Info("Updating maintenance window end time\n")
			existingMW.SetEndTime(endTime)
		}

		if cmd.Flags().Changed("alert-target-uuids") {
			alertTargetUUIDsString, err := cmd.Flags().GetString("alert-target-uuids")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			alertFilter := existingMW.GetAlertConfigurationFilter()
			target := alertFilter.GetTarget()
			alertTargetUUIDs := make([]string, 0)
			alertUseAllTargets := false
			if len(strings.TrimSpace(alertTargetUUIDsString)) > 0 {
				alertTargetUUIDs = strings.Split(alertTargetUUIDsString, ",")
				alertUseAllTargets = false
			} else {
				alertUseAllTargets = true
			}
			target.Uuids = util.StringSliceFromString(alertTargetUUIDs)
			target.All = util.GetBoolPointer(alertUseAllTargets)
			alertFilter.SetTarget(target)
			existingMW.SetAlertConfigurationFilter(alertFilter)
		}

		if cmd.Flags().Changed("health-check-universe-uuids") {
			healthTargetUUIDsString, err := cmd.Flags().GetString("health-check-universe-uuids")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			healthTargetUUIDs := make([]string, 0)
			healthUseAllTargets := false
			suppressHealth := existingMW.GetSuppressHealthCheckNotificationsConfig()
			if len(strings.TrimSpace(healthTargetUUIDsString)) > 0 {
				healthTargetUUIDs = strings.Split(healthTargetUUIDsString, ",")
				healthUseAllTargets = false
			} else {
				healthUseAllTargets = true
			}
			suppressHealth.UniverseUUIDSet = util.StringSliceFromString(healthTargetUUIDs)
			suppressHealth.SuppressAllUniverses = util.GetBoolPointer(healthUseAllTargets)
			existingMW.SetSuppressHealthCheckNotificationsConfig(suppressHealth)
		}

		markAsComplete, err := cmd.Flags().GetBool("mark-as-complete")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if markAsComplete {
			logrus.Info("Updating maintenance window to mark as complete\n")
			existingMW.SetEndTime(time.Now())
		}

		mw, response, err := authAPI.UpdateMaintenanceWindow(uuid).
			UpdateMaintenanceWindowRequest(existingMW).
			Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response, err, "Maintenance Window", "Update")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}
		r := []ybaclient.MaintenanceWindow{mw}

		logrus.Infof("The maintenance window %s (%s) has been updated\n",
			formatter.Colorize(name, formatter.GreenColor), mw.GetUuid())

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
	updateMaintenanceWindowCmd.Flags().SortFlags = false

	updateMaintenanceWindowCmd.Flags().
		StringP("uuid", "u", "", "[Required] UUID of the maintenance window.")
	updateMaintenanceWindowCmd.MarkFlagRequired("uuid")

	updateMaintenanceWindowCmd.Flags().
		String("name", "", "[Optional] Update name of the maintenance window.")
	updateMaintenanceWindowCmd.Flags().
		String("description", "", "[Optional] Update description of the maintenance window.")
	updateMaintenanceWindowCmd.Flags().
		String("start-time", "",
			"[Optional] Update ISO 8601 (YYYY-MM-DDTHH:MM:SSZ) format for start time of the maintenance window.")
	updateMaintenanceWindowCmd.Flags().
		String("end-time", "",
			"[Optional] UpdateISO 8601 (YYYY-MM-DDTHH:MM:SSZ) format for end time of the maintenance window.")
	updateMaintenanceWindowCmd.Flags().String("alert-target-uuids", "",
		"[Optional] Update comma separated list of universe UUIDs to suppress alerts. "+
			"If left empty, suppress alerts on all the universes (including future universes).")
	updateMaintenanceWindowCmd.Flags().String("health-check-universe-uuids", "",
		"[Optional] Update comma separated list of universe UUIDs to suppress health checks. "+
			"If left empty, suppress health check notifications on all the universes (including future universes).")
	updateMaintenanceWindowCmd.Flags().
		Bool("mark-as-complete", false, "[Optional] Mark the maintenance window as complete.")
}
