/*
 * Copyright (c) YugabyteDB, Inc.
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
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/backup/commonbackupinfo"
)

// listIncrementalBackupsCmd represents the universe backup command
var listIncrementalBackupsCmd = &cobra.Command{
	Use:     "list-increments",
	Short:   "List the incremental backups of a backup",
	Long:    "List incremental backups of YugabyteDB Anywhere universe backup",
	Example: `yba backup list-increments --backup-uuid <backup-uuid>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		backupUUID, err := cmd.Flags().GetString("backup-uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(backupUUID) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize(
					"No backup UUID specified to list incremental backup\n",
					formatter.RedColor,
				),
			)
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
			util.FatalHTTPError(response, err, "Backup", "List Incrementals")
		}

		storageConfigListRequest := authAPI.GetListOfCustomerConfig()
		rList, response, err := storageConfigListRequest.Execute()
		if err != nil {
			util.FatalHTTPError(
				response,
				err,
				"Backup",
				"List Incrementals - Get Storage Configuration",
			)
		}

		commonbackupinfo.StorageConfigs = make([]ybaclient.CustomerConfigUI, 0)
		for _, s := range rList {
			if strings.Compare(s.GetType(), util.StorageCustomerConfigType) == 0 {
				commonbackupinfo.StorageConfigs = append(commonbackupinfo.StorageConfigs, s)
			}
		}

		if len(r) > 0 && util.IsOutputType(formatter.TableFormatKey) {
			fullCommonBackupInfoContext := *commonbackupinfo.NewFullCommonBackupInfoContext()
			fullCommonBackupInfoContext.Output = os.Stdout
			fullCommonBackupInfoContext.Format = commonbackupinfo.NewFullCommonBackupInfoFormat(
				viper.GetString("output"))
			for i, cbi := range r {
				fullCommonBackupInfoContext.SetFullCommonBackupInfo(cbi)
				fullCommonBackupInfoContext.Write(i)
			}

			return
		}

		commonBackupInfoCtx := formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  commonbackupinfo.NewCommonBackupInfoFormat(viper.GetString("output")),
		}
		commonbackupinfo.Write(commonBackupInfoCtx, r)
	},
}

func init() {
	listIncrementalBackupsCmd.Flags().SortFlags = false
	listIncrementalBackupsCmd.Flags().String("backup-uuid", "",
		"[Required] Base backup uuid to list incremental backups.")
	listIncrementalBackupsCmd.MarkFlagRequired("backup-uuid")

}
