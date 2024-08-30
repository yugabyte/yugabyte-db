/*
 * Copyright (c) YugaByte, Inc.
 */

package backup

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
)

// describeBackupCmd represents the edit backup command
var describeBackupCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	Short:   "Describe a YugabyteDB Anywhere universe backup",
	Long:    "Describe an universe backup in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		backupUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(backupUUID) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No backup UUID specified to describe backup\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		backupUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		var limit int32 = 10
		var offset int32 = 0

		backupUUIDList := []string{backupUUID}
		backupAPIFilter := ybaclient.BackupApiFilter{
			BackupUUIDList: backupUUIDList,
		}

		backupAPIDirection := "DESC"
		backupAPISort := "createTime"

		backupAPIQuery := ybaclient.BackupPagedApiQuery{
			Filter:    backupAPIFilter,
			Direction: backupAPIDirection,
			Limit:     limit,
			Offset:    offset,
			SortBy:    backupAPISort,
		}

		backupListRequest := authAPI.ListBackups().PageBackupsRequest(backupAPIQuery)
		r, response, err := backupListRequest.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Backup", "Describe")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		storageConfigListRequest := authAPI.GetListOfCustomerConfig()
		rList, response, err := storageConfigListRequest.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response, err, "Backup", "Get - Get Storage Configuration")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		backup.StorageConfigs = make([]ybaclient.CustomerConfigUI, 0)
		for _, s := range rList {
			if strings.Compare(s.GetType(), util.StorageCustomerConfigType) == 0 {
				backup.StorageConfigs = append(backup.StorageConfigs, s)
			}
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
					fmt.Sprintf("No backups with UUID: %s found\n", backupUUID),
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
	describeBackupCmd.Flags().SortFlags = false
	describeBackupCmd.Flags().String("uuid", "",
		"[Required] The UUID of the backup to be described.")
	describeBackupCmd.MarkFlagRequired("uuid")
}
