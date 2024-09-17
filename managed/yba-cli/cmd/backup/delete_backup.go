/*
 * Copyright (c) YugaByte, Inc.
 */

package backup

import (
	"fmt"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// deleteBackupCmd represents the universe backup command
var deleteBackupCmd = &cobra.Command{
	Use:   "delete",
	Short: "Delete a YugabyteDB Anywhere universe backup",
	Long:  "Delete an universe backup in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		backupInfoArray, err := cmd.Flags().GetStringArray("backup-info")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(backupInfoArray) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No backup info specified to delete backup(s)\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		backupInfoArray, err := cmd.Flags().GetStringArray("backup-info")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		deleteBackupParams := ybaclient.DeleteBackupParams{
			DeleteBackupInfos: buildBackupInfo(backupInfoArray),
		}

		backupDeleteRequest := authAPI.DeleteBackups().DeleteBackup(deleteBackupParams)
		_, response, err := backupDeleteRequest.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Backup", "Delete")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		logrus.Infof(
			formatter.Colorize(
				"The backups have been submitted for deletion.\n",
				formatter.GreenColor),
		)
	},
}

func buildBackupInfo(backupInfos []string) []ybaclient.DeleteBackupInfo {
	var res []ybaclient.DeleteBackupInfo

	for _, backupInfo := range backupInfos {
		backupDetails := map[string]string{}
		for _, backupDetailString := range strings.Split(backupInfo, ",") {
			kvp := strings.Split(backupDetailString, "=")
			if len(kvp) != 2 {
				logrus.Fatalln(
					formatter.Colorize("Incorrect format in backup info description.",
						formatter.RedColor))
			}
			key := kvp[0]
			val := kvp[1]
			switch key {
			case "backup-uuid":
				if len(strings.TrimSpace(val)) != 0 {
					backupDetails["backup-uuid"] = val
				} else {
					logrus.Fatalf(
						formatter.Colorize("Backup UUID cannot be empty",
							formatter.RedColor))
				}
			case "storage-config-uuid":
				backupDetails["storage-config-uuid"] = ""
				if len(strings.TrimSpace(val)) != 0 {
					backupDetails["storage-config-uuid"] = val
				}
			}
		}

		r := ybaclient.DeleteBackupInfo{
			BackupUUID: backupDetails["backup-uuid"],
		}
		value, exists := backupDetails["storage-config-uuid"]
		if exists {
			r.SetStorageConfigUUID(value)
		}
		res = append(res, r)
	}
	return res
}

func init() {
	deleteBackupCmd.Flags().SortFlags = false
	deleteBackupCmd.Flags().StringArray("backup-info", []string{},
		fmt.Sprintf("[Required] The info of the backups to be described. The backup-info is of the "+
			"format backup-uuid=<backup_uuid>,storage-config-uuid=<storage-config-uuid>. %s.",
			formatter.Colorize("Backup UUID is required.",
				formatter.GreenColor)))
	deleteBackupCmd.MarkFlagRequired("backup-info")
}
