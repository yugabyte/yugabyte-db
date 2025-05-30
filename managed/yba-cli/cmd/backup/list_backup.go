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

// listBackupCmd represents the list backup command
var listBackupCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List YugabyteDB Anywhere backups",
	Long:    "List backups in YugabyteDB Anywhere",
	Example: `yba backup list --universe-uuids <universe-uuid-1>,<universe-uuid-2> \
	--universe-names <universe-name-1>,<universe-name-2>`,
	Run: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		universeUUIDs, err := cmd.Flags().GetString("universe-uuids")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		universeNames, err := cmd.Flags().GetString("universe-names")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		storageConfigListRequest := authAPI.GetListOfCustomerConfig()
		rList, response, err := storageConfigListRequest.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response, err, "Backup", "List - Get Storage Configuration")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		backup.StorageConfigs = make([]ybaclient.CustomerConfigUI, 0)
		for _, s := range rList {
			if strings.Compare(s.GetType(), util.StorageCustomerConfigType) == 0 {
				backup.StorageConfigs = append(backup.StorageConfigs, s)
			}
		}

		backup.KMSConfigs = make([]util.KMSConfig, 0)
		kmsConfigs, response, err := authAPI.ListKMSConfigs().Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err,
				"Backup", "List - Get KMS Configurations")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		for _, k := range kmsConfigs {
			kmsConfig, err := util.ConvertToKMSConfig(k)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			backup.KMSConfigs = append(backup.KMSConfigs, kmsConfig)
		}

		backupCtx := formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  backup.NewBackupFormat(viper.GetString("output")),
		}

		var limit int32 = 10
		var offset int32 = 0

		backupAPIFilter := ybaclient.BackupApiFilter{}
		if (len(strings.TrimSpace(universeNames))) > 0 {
			backupAPIFilter.SetUniverseNameList(strings.Split(universeNames, ","))
		}

		if (len(strings.TrimSpace(universeUUIDs))) > 0 {
			backupAPIFilter.SetUniverseUUIDList(strings.Split(universeUUIDs, ","))
		}

		backupAPIDirection := util.DescSortDirection
		backupAPISort := "createTime"

		backupAPIQuery := ybaclient.BackupPagedApiQuery{
			Filter:    backupAPIFilter,
			Direction: backupAPIDirection,
			Limit:     limit,
			Offset:    offset,
			SortBy:    backupAPISort,
		}

		backupListRequest := authAPI.ListBackups().PageBackupsRequest(backupAPIQuery)
		backups := make([]ybaclient.BackupResp, 0)
		force := viper.GetBool("force")
		for {
			// Execute backup list request
			r, response, err := backupListRequest.Execute()
			if err != nil {
				errMessage := util.ErrorFromHTTPResponse(response, err, "Backup", "List")
				logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
			}

			// Check if backups found
			if len(r.GetEntities()) < 1 {
				if util.IsOutputType(formatter.TableFormatKey) {
					logrus.Info("No backups found\n")
				} else {
					logrus.Info("[]\n")
				}
				return
			}

			// Write backup entities
			if force {
				backups = append(backups, r.GetEntities()...)
			} else {
				backup.Write(backupCtx, r.GetEntities())
			}

			// Check if there are more pages
			hasNext := r.GetHasNext()
			if !hasNext {
				if util.IsOutputType(formatter.TableFormatKey) && !force {
					logrus.Info("No more backups present\n")
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
			backupAPIQuery.Offset = offset
			backupListRequest = authAPI.ListBackups().PageBackupsRequest(backupAPIQuery)
		}
		if force {
			backup.Write(backupCtx, backups)
		}
	},
}

func init() {
	listBackupCmd.Flags().SortFlags = false
	listBackupCmd.Flags().String("universe-uuids", "",
		"[Optional] Comma separated list of universe uuids")
	listBackupCmd.Flags().String("universe-names", "",
		"[Optional] Comma separated list of universe names")
	listBackupCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
