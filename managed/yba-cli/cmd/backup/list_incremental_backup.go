/*
 * Copyright (c) YugaByte, Inc.
 */

package backup

import (
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

// listIncrementalBackupsCmd represents the universe backup command
var listIncrementalBackupsCmd = &cobra.Command{
	Use:   "list-increments",
	Short: "List the incremental backups of a backup",
	Long:  "List incremental backups of YugabyteDB Anywhere universe backup",
	PreRun: func(cmd *cobra.Command, args []string) {
		backupUUID, err := cmd.Flags().GetString("backup-uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(backupUUID) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No backup UUID specified to list incremental backup\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		backupUUID, err := cmd.Flags().GetString("backup-uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		listIncrementalBackupRequest := authAPI.ListIncrementalBackups(backupUUID)

		r, response, err := listIncrementalBackupRequest.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Backup", "List Incrementals")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		storageConfigListRequest := authAPI.GetListOfCustomerConfig()
		rList, response, err := storageConfigListRequest.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response, err, "Backup", "List Incrementals - Get Storage Configuration")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		backup.StorageConfigs = make([]ybaclient.CustomerConfigUI, 0)
		for _, s := range rList {
			if strings.Compare(s.GetType(), util.StorageCustomerConfigType) == 0 {
				backup.StorageConfigs = append(backup.StorageConfigs, s)
			}
		}

		commonBackupInfoContext := *backup.NewCommonBackupInfoContext()
		commonBackupInfoContext.Output = os.Stdout
		commonBackupInfoContext.Format = backup.NewCommonBackupInfoFormat(viper.GetString("output"))
		for index, value := range r {
			commonBackupInfoContext.SetCommonBackupInfo(value)
			commonBackupInfoContext.Write(index)
		}
		return

	},
}

func init() {
	listIncrementalBackupsCmd.Flags().SortFlags = false
	listIncrementalBackupsCmd.Flags().String("backup-uuid", "",
		"[Required] Base backup uuid to list incremental backups.")
	listIncrementalBackupsCmd.MarkFlagRequired("backup-uuid")

}
