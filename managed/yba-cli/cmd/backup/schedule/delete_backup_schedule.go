/*
* Copyright (c) YugaByte, Inc.
 */

package schedule

import (
	"fmt"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// deleteBackupScheduleCmd represents the schedule command
var deleteBackupScheduleCmd = &cobra.Command{
	Use:   "delete",
	Short: "Delete a YugabyteDB Anywhere universe backup schedule",
	Long:  "Delete an universe backup schedule in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		scheduleName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(scheduleName) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No backup schedule name found to delete\n", formatter.RedColor))
		}
		err = util.ConfirmCommand(
			fmt.Sprintf("Are you sure you want to delete %s: %s", "Backup Schedule", scheduleName),
			viper.GetBool("force"))
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		scheduleName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		scheduleListRequest := authAPI.ListBackupSchedules()

		var limit int32 = 10
		var offset int32 = 0

		scheduleAPIFilter := ybaclient.ScheduleApiFilter{}

		scheduleAPIDirection := "DESC"
		scheduleAPISort := "scheduleName"

		universeName, err := cmd.Flags().GetString("universe-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if (len(strings.TrimSpace(universeName))) > 0 {
			universeListRequest := authAPI.ListUniverses()
			universeName, _ := cmd.Flags().GetString("name")
			universeListRequest = universeListRequest.Name(universeName)

			r, response, err := universeListRequest.Execute()
			if err != nil {
				errMessage := util.ErrorFromHTTPResponse(
					response, err, "Backup Schedule", "Delete - Get Universe")
				logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
			}
			if len(r) < 1 {
				logrus.Fatalf(
					formatter.Colorize(
						fmt.Sprintf("No universes with name: %s found\n", universeName),
						formatter.RedColor,
					))
			}
			scheduleAPIFilter.SetUniverseUUIDList([]string{r[0].GetUniverseUUID()})
		}

		scheduleAPIQuery := ybaclient.SchedulePagedApiQuery{
			Filter:    scheduleAPIFilter,
			Direction: scheduleAPIDirection,
			Limit:     limit,
			Offset:    offset,
			SortBy:    scheduleAPISort,
		}

		scheduleListRequest = scheduleListRequest.PageScheduleRequest(scheduleAPIQuery)

		scheduleUUID := ""

		for {
			// Execute backup list request
			r, response, err := scheduleListRequest.Execute()
			if err != nil {
				errMessage := util.ErrorFromHTTPResponse(response, err,
					"Backup Schedule", "Delete - List Schedules")
				logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
			}

			// Check if backups found
			if len(r.GetEntities()) < 1 {
				logrus.Fatalf(
					formatter.Colorize(
						fmt.Sprintf("No schedules with name: %s found\n", scheduleName),
						formatter.RedColor,
					))

			}

			for _, schedule := range r.GetEntities() {
				if strings.Compare(schedule.GetScheduleName(), scheduleName) == 0 {
					scheduleUUID = schedule.GetScheduleUUID()
					break
				}
			}

			// Check if there are more pages
			hasNext := r.GetHasNext()
			if !hasNext {
				break
			}

			offset += int32(len(r.GetEntities()))

			// Prepare next page request
			scheduleAPIQuery.Offset = offset
			scheduleListRequest = authAPI.ListBackupSchedules().PageScheduleRequest(scheduleAPIQuery)
		}

		if len(scheduleUUID) == 0 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No schedules with name: %s found\n", scheduleName),
					formatter.RedColor,
				))
		}

		deleteScheduleRequest := authAPI.DeleteBackupSchedule(scheduleUUID)

		rDelete, response, err := deleteScheduleRequest.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Backup Schedule", "Delete")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		if rDelete.GetSuccess() {
			logrus.Info(fmt.Sprintf("The schedule %s (%s) has been deleted",
				formatter.Colorize(scheduleName, formatter.GreenColor), scheduleUUID))

		} else {
			logrus.Errorf(
				formatter.Colorize(
					fmt.Sprintf(
						"An error occurred while deleting schedule %s (%s)\n",
						formatter.Colorize(scheduleName, formatter.GreenColor), scheduleUUID),
					formatter.RedColor))
		}
	},
}

func init() {
	deleteBackupScheduleCmd.Flags().SortFlags = false
	deleteBackupScheduleCmd.Flags().StringP("name", "n", "",
		"[Required] The name of the schedule to be deleted.")
	deleteBackupScheduleCmd.MarkFlagRequired("name")
	deleteBackupScheduleCmd.Flags().String("universe-name", "",
		"[Optional] The name of the universe containing the schedule.")
	deleteBackupScheduleCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
