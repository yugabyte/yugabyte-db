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
	Use:     "update",
	Aliases: []string{"edit"},
	Short:   "Edit a YugabyteDB Anywhere universe backup",
	Long:    "Edit an universe backup in YugabyteDB Anywhere",
	Example: `yba backup update --uuid <backup-uuid> \
	--time-before-delete-in-ms <time-before-delete-in-ms> \
	--storage-config-name <storage-config-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		backupUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(backupUUID) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No backup UUID specified to edit backup\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		backupUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		requestBody := ybaclient.EditBackupParams{}

		if cmd.Flags().Changed("time-before-delete-in-ms") {
			logrus.Debugf("Updating time-before-delete-in-ms")
			timeBeforeDeleteFromPresentInMillis, err := cmd.Flags().GetInt64("time-before-delete-in-ms")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}

			requestBody.SetTimeBeforeDeleteFromPresentInMillis(timeBeforeDeleteFromPresentInMillis)
			requestBody.SetExpiryTimeUnit("MILLISECONDS")

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
					response, err, "Backup", "Edit - Get Storage Configuration")
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
				logrus.Fatalf(
					formatter.Colorize(
						fmt.Sprintf("No storage configurations with name: %s found\n",
							storageConfigName),
						formatter.RedColor,
					))
				return
			}

			if len(r) > 0 {
				storageUUID = r[0].GetConfigUUID()
				requestBody.SetStorageConfigUUID(storageUUID)
				if len(backup.StorageConfigs) == 0 {
					backup.StorageConfigs = make([]ybaclient.CustomerConfigUI, 0)
				}
				backup.StorageConfigs = append(backup.StorageConfigs, r[0])
			}
		}
		editBackupRequest := authAPI.EditBackup(backupUUID).Backup(requestBody)
		_, response, err := editBackupRequest.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Backup", "Edit")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		backupUUIDList := []string{backupUUID}
		backupAPIFilter := ybaclient.BackupApiFilter{
			BackupUUIDList: backupUUIDList,
		}

		var limit int32 = 10
		var offset int32 = 0
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
			errMessage := util.ErrorFromHTTPResponse(response, err, "Backup", "Edit - Describe Backup")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		fullBackupContext := *backup.NewFullBackupContext()
		fullBackupContext.Output = os.Stdout
		fullBackupContext.Format = backup.NewBackupFormat(viper.GetString("output"))
		fullBackupContext.SetFullBackup(r.GetEntities()[0])
		fullBackupContext.Write()
	},
}

func init() {
	editBackupCmd.Flags().SortFlags = false
	editBackupCmd.Flags().String("uuid", "",
		"[Required] The UUID of the backup to be edited.")
	editBackupCmd.MarkFlagRequired("uuid")
	editBackupCmd.Flags().Int64("time-before-delete-in-ms", 0,
		"[Optional] Time before delete from the current time in ms.")
	editBackupCmd.Flags().String("storage-config-name", "",
		"[Optional] Change the storage configuration assigned to the backup.")

}
