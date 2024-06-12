/*
 * Copyright (c) YugaByte, Inc.
 */

package backup

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
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/backup"
)

// getBackupCmd represents the edit backup command
var getBackupCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a YugabyteDB Anywhere universe backup",
	Long:    "Describe an universe backup in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		backupUUID, err := cmd.Flags().GetString("backup-uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(backupUUID) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No backup uuid specified to describe backup\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI, err := ybaAuthClient.NewAuthAPIClient()
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		authAPI.GetCustomerUUID()

		backupUUID, err := cmd.Flags().GetString("backup-uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		var limit int32 = 10
		var offset int32 = 0

		backupUUIDList := []string{backupUUID}
		backupApiFilter := ybaclient.BackupApiFilter{
			BackupUUIDList: backupUUIDList,
		}

		backupApiDirection := "DESC"
		backupApiSort := "createTime"

		backupApiQuery := ybaclient.BackupPagedApiQuery{
			Filter:    backupApiFilter,
			Direction: backupApiDirection,
			Limit:     limit,
			Offset:    offset,
			SortBy:    backupApiSort,
		}

		backupListRequest := authAPI.ListBackups().PageBackupsRequest(backupApiQuery)
		r, response, err := backupListRequest.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Backup", "Describe Backup")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		if len(r.GetEntities()) > 0 && util.IsOutputType("table") {
			fullBackupContext := *backup.NewFullBackupContext()
			fullBackupContext.Output = os.Stdout
			fullBackupContext.Format = backup.NewBackupFormat(viper.GetString("output"))
			fullBackupContext.SetFullBackup(r.GetEntities()[0])
			fullBackupContext.Write()
			return
		}

		if len(r.GetEntities()) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No backups with uuid: %s found\n", backupUUID),
					formatter.RedColor,
				))
		}

		backupCtx := formatter.Context{
			Output: os.Stdout,
			Format: backup.NewBackupFormat(viper.GetString("output")),
		}
		backup.Write(backupCtx, r.GetEntities())
	},
}

func init() {
	getBackupCmd.Flags().SortFlags = false
	getBackupCmd.Flags().String("backup-uuid", "",
		"[Required] The uuid of the backup to be described.")
	getBackupCmd.MarkFlagRequired("backup-uuid")
}
