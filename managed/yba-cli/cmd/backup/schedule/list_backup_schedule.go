/*
* Copyright (c) YugabyteDB, Inc.
 */

package schedule

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
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/backup/schedule"
)

// listBackupScheduleCmd represents the list backup command
var listBackupScheduleCmd = &cobra.Command{
	Use:   "list",
	Short: "List YugabyteDB Anywhere universe backup schedules",
	Long:  "List backup schedules of a universe in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

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

		fetchStorageAndKMSConfigurationsForListing(authAPI, "List")

		backupScheduleCtx := formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  schedule.NewScheduleFormat(viper.GetString("output")),
		}

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
		force := viper.GetBool("force")
		for {
			// Execute backup schedule list request
			r, err := authAPI.ListBackupSchedulesRest(backupScheduleAPIQuery, "List")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}

			// Check if backup schedules found
			if len(r.GetEntities()) < 1 {
				if util.IsOutputType(formatter.TableFormatKey) {
					logrus.Infof("No backup schedules found for %s universe\n", universeName)
				} else {
					logrus.Info("[]\n")
				}
				return
			}

			// Write backup schedule entities
			if force {
				backupSchedules = append(backupSchedules, r.GetEntities()...)
			} else {
				schedule.Write(backupScheduleCtx, r.GetEntities())
			}

			// Check if there are more pages
			hasNext := r.GetHasNext()
			if !hasNext {
				if util.IsOutputType(formatter.TableFormatKey) && !force {
					logrus.Info("No more backup schedules present for this universe\n")
				}
				break
			}

			err = util.ConfirmCommand(
				"List more entries",
				viper.GetBool("force"))
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}

			offset += int32(len(r.GetEntities()))

			// Prepare next page request
			backupScheduleAPIQuery.Offset = offset
		}
		if force {
			schedule.Write(backupScheduleCtx, backupSchedules)
		}
	},
}

func init() {
	listBackupScheduleCmd.Flags().SortFlags = false
	listBackupScheduleCmd.Flags().String("universe-name", "",
		"[Required] Universe name whose backupSchedule schedules are to be listed.")
	listBackupScheduleCmd.MarkFlagRequired("universe-name")
	listBackupScheduleCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
