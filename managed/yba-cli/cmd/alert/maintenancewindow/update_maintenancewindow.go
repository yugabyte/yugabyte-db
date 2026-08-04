/*
 * Copyright (c) YugabyteDB, Inc.
 */

package maintenancewindow

import (
	"fmt"
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
		if util.IsEmptyString(uuid) {
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
			util.FatalHTTPError(
				response,
				err,
				"Maintenance Window",
				"Update - Get Maintenance Window",
			)
		}

		existingMaintenanceWindow := util.CheckAndDereference(
			existingMW,
			fmt.Sprintf("No maintenance window with uuid: %s found", uuid),
		)

		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(name) {
			logrus.Info("Updating maintenance window name\n")
			existingMaintenanceWindow.SetName(name)
		}

		description, err := cmd.Flags().GetString("description")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(description) {
			logrus.Info("Updating maintenance window description\n")
			existingMaintenanceWindow.SetDescription(description)
		}

		startTimeString, err := cmd.Flags().GetString("start-time")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(startTimeString) {
			startTime, err := time.Parse(time.RFC3339, startTimeString)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			logrus.Info("Updating maintenance window start time\n")
			existingMaintenanceWindow.SetStartTime(startTime)
		}

		endTimeString, err := cmd.Flags().GetString("end-time")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if !util.IsEmptyString(endTimeString) {
			endTime, err := time.Parse(time.RFC3339, endTimeString)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if endTime.Before(existingMaintenanceWindow.GetStartTime()) {
				logrus.Fatalf(
					formatter.Colorize(
						"End time cannot be before start time\n",
						formatter.RedColor,
					),
				)
			}
			logrus.Info("Updating maintenance window end time\n")
			existingMaintenanceWindow.SetEndTime(endTime)
		}

		if cmd.Flags().Changed("alert-target-uuids") {
			alertTargetUUIDsString, err := cmd.Flags().GetString("alert-target-uuids")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			alertFilter := existingMaintenanceWindow.GetAlertConfigurationFilter()
			target := alertFilter.GetTarget()
			alertTargetUUIDs := make([]string, 0)
			alertUseAllTargets := false
			if !util.IsEmptyString(alertTargetUUIDsString) {
				alertTargetUUIDs = strings.Split(alertTargetUUIDsString, ",")
				alertUseAllTargets = false
			} else {
				alertUseAllTargets = true
			}
			target.Uuids = alertTargetUUIDs
			target.All = util.GetBoolPointer(alertUseAllTargets)
			alertFilter.SetTarget(target)
			existingMaintenanceWindow.SetAlertConfigurationFilter(alertFilter)
		}

		if cmd.Flags().Changed("health-check-universe-uuids") {
			healthTargetUUIDsString, err := cmd.Flags().GetString("health-check-universe-uuids")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			healthTargetUUIDs := make([]string, 0)
			healthUseAllTargets := false
			suppressHealth := existingMaintenanceWindow.GetSuppressHealthCheckNotificationsConfig()
			if !util.IsEmptyString(healthTargetUUIDsString) {
				healthTargetUUIDs = strings.Split(healthTargetUUIDsString, ",")
				healthUseAllTargets = false
			} else {
				healthUseAllTargets = true
			}
			suppressHealth.UniverseUUIDSet = healthTargetUUIDs
			suppressHealth.SuppressAllUniverses = util.GetBoolPointer(healthUseAllTargets)
			existingMaintenanceWindow.SetSuppressHealthCheckNotificationsConfig(suppressHealth)
		}

		markAsComplete, err := cmd.Flags().GetBool("mark-as-complete")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if markAsComplete {
			logrus.Info("Updating maintenance window to mark as complete\n")
			existingMaintenanceWindow.SetEndTime(time.Now())
		}

		rMW, response, err := authAPI.UpdateMaintenanceWindow(uuid).
			UpdateMaintenanceWindowRequest(existingMaintenanceWindow).
			Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Maintenance Window", "Update")
		}

		updatedMaintenanceWindow := util.CheckAndDereference(
			rMW,
			"An error occurred while updating maintenance window with uuid "+uuid,
		)

		r := []ybaclient.MaintenanceWindow{updatedMaintenanceWindow}

		logrus.Infof("The maintenance window %s (%s) has been updated\n",
			formatter.Colorize(name, formatter.GreenColor), updatedMaintenanceWindow.GetUuid())

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
