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

// listBackupCmd represents the list backup command
var listBackupCmd = &cobra.Command{
	Use:   "list",
	Short: "List YugabyteDB Anywhere backups",
	Long:  "List backups in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
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

		backupCtx := formatter.Context{
			Output: os.Stdout,
			Format: backup.NewBackupFormat(viper.GetString("output")),
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

		for {
			// Execute backup list request
			r, response, err := backupListRequest.Execute()
			if err != nil {
				errMessage := util.ErrorFromHTTPResponse(response, err, "Backup", "List")
				logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
			}

			// Check if backups found
			if len(r.GetEntities()) < 1 {
				if util.IsOutputType("table") {
					logrus.Infoln("No backups found\n")
				} else {
					logrus.Infoln("[]\n")
				}
				return
			}

			// Write backup entities
			backup.Write(backupCtx, r.GetEntities())

			// Check if there are more pages
			hasNext := r.GetHasNext()
			if !hasNext {
				logrus.Infoln("No more backups present\n")
				break
			}

			// Prompt user for more entries
			if !promptForMoreEntries() {
				break
			}

			offset += int32(len(r.GetEntities()))

			// Prepare next page request
			backupAPIQuery.Offset = offset
			backupListRequest = authAPI.ListBackups().PageBackupsRequest(backupAPIQuery)
		}
	},
}

// Function to prompt user for more entries
func promptForMoreEntries() bool {
	var input string
	fmt.Print("More entries? (yes/no): ")
	fmt.Scanln(&input)
	return strings.ToLower(input) == "yes"
}

func init() {
	listBackupCmd.Flags().SortFlags = false
	listBackupCmd.Flags().String("universe-uuids", "",
		"[Optional] Comma separated list of universe uuids")
	listBackupCmd.Flags().String("universe-names", "",
		"[Optional] Comma separated list of universe names")
}
