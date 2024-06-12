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

// editBackupCmd represents the edit backup command
var editBackupCmd = &cobra.Command{
	Use:   "edit",
	Short: "Edit a YugabyteDB Anywhere universe backup",
	Long:  "Edit an universe backup in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		backupUUID, err := cmd.Flags().GetString("backup-uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(backupUUID) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No backup uuid specified to edit backup\n", formatter.RedColor))
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

		timeBeforeDeleteFromPresentInMillis, err := cmd.Flags().GetInt64("time-before-delete-in-ms")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		requestBody := ybaclient.EditBackupParams{
			TimeBeforeDeleteFromPresentInMillis: util.GetInt64Pointer(int64(timeBeforeDeleteFromPresentInMillis)),
			ExpiryTimeUnit:                      util.GetStringPointer("MILLISECONDS"),
		}

		storageConfigName, err := cmd.Flags().GetString("storage-config-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		var storageUUID string
		if len(strings.TrimSpace(storageConfigName)) != 0 {
			storageConfigListRequest := authAPI.GetListOfCustomerConfig()
			r, response, err := storageConfigListRequest.Execute()
			if err != nil {
				errMessage := util.ErrorFromHTTPResponse(
					response, err, "Storage Configuration", "Describe")
				logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
			}

			storageConfigs := make([]ybaclient.CustomerConfigUI, 0)
			for _, s := range r {
				if strings.Compare(s.GetType(), util.StorageCustomerConfigType) == 0 {
					storageConfigs = append(storageConfigs, s)
				}
			}
			storageConfigsName := make([]ybaclient.CustomerConfigUI, 0)
			for _, s := range storageConfigs {
				if strings.Compare(s.GetConfigName(), storageConfigName) == 0 {
					storageConfigsName = append(storageConfigsName, s)
				}
			}
			r = storageConfigsName

			if len(r) < 1 {
				fmt.Println("No storage configurations found")
			}

			if len(r) > 0 {
				storageUUID = r[0].GetConfigUUID()
				requestBody.SetStorageConfigUUID(storageUUID)
			}
		}
		editBackupRequest := authAPI.EditBackup(backupUUID).Backup(requestBody)
		_, response, err := editBackupRequest.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Backup", "Edit")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		backupUUIDList := []string{backupUUID}
		backupApiFilter := ybaclient.BackupApiFilter{
			BackupUUIDList: backupUUIDList,
		}

		var limit int32 = 10
		var offset int32 = 0
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

		fullBackupContext := *backup.NewFullBackupContext()
		fullBackupContext.Output = os.Stdout
		fullBackupContext.Format = backup.NewBackupFormat(viper.GetString("output"))
		fullBackupContext.SetFullBackup(r.GetEntities()[0])
		fullBackupContext.Write()
		return
	},
}

func init() {
	editBackupCmd.Flags().SortFlags = false
	editBackupCmd.Flags().String("backup-uuid", "",
		"[Required] The uuid of the backup to be described.")
	editBackupCmd.MarkFlagRequired("backup-uuid")
	editBackupCmd.Flags().Int64("time-before-delete-in-ms", 0,
		"[Required] Time before delete from the current time in ms")
	editBackupCmd.MarkFlagRequired("time-before-delete-in-ms")
	editBackupCmd.Flags().String("storage-config-name", "",
		"[Optional] Change the storage config assigned to the backup")

}
