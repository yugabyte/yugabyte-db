/*
* Copyright (c) YugabyteDB, Inc.
 */

package schedule

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
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/backup"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/backup/schedule"
)

// describeBackupScheduleCmd represents the describe backup command
var describeBackupScheduleCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a YugabyteDB Anywhere universe backup schedule",
	Long:    "Describe a backup schedule of a universe in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		scheduleName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(scheduleName) == 0 {
			logrus.Fatal(
				formatter.Colorize(
					"No schedule name specified to describe schedule backup\n",
					formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		scheduleName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		universeName, err := cmd.Flags().GetString("universe-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		universeUUIDs := make([]string, 0)

		if !util.IsEmptyString(universeName) {
			universeListRequest := authAPI.ListUniverses()
			universeListRequest = universeListRequest.Name(universeName)
			r, response, err := universeListRequest.Execute()
			if err != nil {
				util.FatalHTTPError(response, err, "Backup Schedule", "List - List Universes")
			}
			if len(r) < 1 {
				logrus.Fatalf(
					formatter.Colorize(fmt.Sprintf("Universe with name %s not found", universeName),
						formatter.RedColor))
			} else if len(r) > 1 {
				logrus.Fatalf(
					formatter.Colorize(
						fmt.Sprintf("Multiple universes with same name %s found", universeName),
						formatter.RedColor))
			}
			universeUUIDs = append(universeUUIDs, r[0].GetUniverseUUID())
		}

		fetchStorageAndKMSConfigurationsForListing(authAPI, "Describe")

		var limit int32 = 10
		var offset int32 = 0

		backupScheduleAPIFilter := ybaclient.ScheduleApiFilter{}
		if len(universeUUIDs) > 0 {
			backupScheduleAPIFilter.SetUniverseUUIDList(universeUUIDs)
		}

		backupScheduleAPIDirection := util.DescSortDirection
		backupScheduleAPISort := "scheduleUUID"

		backupScheduleAPIQuery := ybaclient.SchedulePagedApiQuery{
			Filter:    backupScheduleAPIFilter,
			Direction: backupScheduleAPIDirection,
			Limit:     limit,
			Offset:    offset,
			SortBy:    backupScheduleAPISort,
		}

		backupSchedules := make([]util.Schedule, 0)
		for {
			// Execute backup schedule describe request
			r, err := authAPI.ListBackupSchedulesRest(backupScheduleAPIQuery, "Describe")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}

			// Check if backup schedules found
			if len(r.GetEntities()) < 1 {
				break
			}

			for _, e := range r.GetEntities() {
				if strings.Compare(e.GetScheduleName(), scheduleName) == 0 {
					backupSchedules = append(backupSchedules, e)
				}
			}

			// Check if there are more pages
			hasNext := r.GetHasNext()
			if !hasNext {
				break
			}

			offset += int32(len(r.GetEntities()))

			// Prepare next page request
			backupScheduleAPIQuery.Offset = offset
		}
		if len(backupSchedules) > 0 && util.IsOutputType(formatter.TableFormatKey) {
			fullScheduleContext := *schedule.NewFullScheduleContext()
			fullScheduleContext.Output = os.Stdout
			fullScheduleContext.Format = backup.NewBackupFormat(viper.GetString("output"))
			fullScheduleContext.SetFullSchedule(backupSchedules[0])
			fullScheduleContext.Write()
			return
		}

		if len(backupSchedules) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No schedule with name: %s for universe %s found\n",
						scheduleName,
						universeName,
					),
					formatter.RedColor,
				))
		}

		backupScheduleCtx := formatter.Context{
			Command: "describe",
			Output:  os.Stdout,
			Format:  backup.NewBackupFormat(viper.GetString("output")),
		}
		schedule.Write(backupScheduleCtx, backupSchedules)
	},
}

func init() {
	describeBackupScheduleCmd.Flags().SortFlags = false
	describeBackupScheduleCmd.Flags().StringP("name", "n", "",
		"[Required] The name of the schedule to be described.")
	describeBackupScheduleCmd.MarkFlagRequired("name")
	describeBackupScheduleCmd.Flags().String("universe-name", "",
		"[Required] The name of the universe to be described.")
	describeBackupScheduleCmd.MarkFlagRequired("universe-name")
}
