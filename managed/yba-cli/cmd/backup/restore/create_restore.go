/*
 * Copyright (c) YugaByte, Inc.
 */

package restore

import (
	"encoding/json"
	"fmt"
	"net/http"
	"os"
	"strconv"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/backup/restore"
)

// createBackupRestoreCmd represents the universe backup command
var createBackupRestoreCmd = &cobra.Command{
	Use:   "create",
	Short: "Restore a YugabyteDB Anywhere universe backup",
	Long:  "Restore an universe backup in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		universeNameFlag, err := cmd.Flags().GetString("universe-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(universeNameFlag)) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to take a backup\n", formatter.RedColor))
		}

		storageConfigNameFlag, err := cmd.Flags().GetString("storage-config-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(storageConfigNameFlag)) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No storage config name found to take a backup\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		var response *http.Response
		authAPI, err := ybaAuthClient.NewAuthAPIClient()
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		authAPI.GetCustomerUUID()

		universeNameFlag, err := cmd.Flags().GetString("universe-name")

		universeListRequest := authAPI.ListUniverses()
		universeListRequest = universeListRequest.Name(universeNameFlag)

		r, response, err := universeListRequest.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Restore", "Create - Get Universe")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		if len(r) < 1 {
			logrus.Fatalf(fmt.Sprintf("Universe with name %s not found", universeNameFlag))
		} else if len(r) > 1 {
			logrus.Fatalf(fmt.Sprintf("Multiple universes with same name %s found", universeNameFlag))
		}

		var universeUUID string
		if len(r) > 0 {
			universeUUID = r[0].GetUniverseUUID()
		}

		// filter by name
		storageName, err := cmd.Flags().GetString("storage-config-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		storageConfigListRequest := authAPI.GetListOfCustomerConfig()
		rList, response, err := storageConfigListRequest.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response, err, "Restore", "Create - Get Storage Configuration")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		storageConfigs := make([]ybaclient.CustomerConfigUI, 0)
		for _, s := range rList {
			if strings.Compare(s.GetType(), util.StorageCustomerConfigType) == 0 {
				storageConfigs = append(storageConfigs, s)
			}
		}
		storageConfigsName := make([]ybaclient.CustomerConfigUI, 0)
		for _, s := range storageConfigs {
			if strings.Compare(s.GetConfigName(), storageName) == 0 {
				storageConfigsName = append(storageConfigsName, s)
			}
		}
		rList = storageConfigsName

		if len(rList) < 1 {
			fmt.Println("No storage configurations found")
			return
		}

		var storageUUID string
		if len(rList) > 0 {
			storageUUID = rList[0].GetConfigUUID()
		}

		backupInfos, err := cmd.Flags().GetStringArray("keyspace-info")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		data, err := json.Marshal(backupInfos)
		if err != nil {
			fmt.Println("Error marshalling:", err)
			return
		}
		logrus.Debug(string(data))
		var result []ybaclient.BackupStorageInfo = buildBackupInfoList(backupInfos)
		requestBody := ybaclient.RestoreBackupParams{
			UniverseUUID:          universeUUID,
			CustomerUUID:          util.GetStringPointer(authAPI.CustomerUUID),
			StorageConfigUUID:     util.GetStringPointer(storageUUID),
			BackupStorageInfoList: &result,
		}
		data, err = json.Marshal(requestBody)
		if err != nil {
			fmt.Println("Error marshalling:", err)
			return
		}
		logrus.Debug(string(data))

		rCreate, response, err := authAPI.RestoreBackup().Backup(requestBody).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Restore", "Create")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		taskUUID := rCreate.GetTaskUUID()
		msg := fmt.Sprintf("The restore task %s is in progress",
			formatter.Colorize(taskUUID, formatter.GreenColor))

		if viper.GetBool("wait") {
			if taskUUID != "" {
				logrus.Info(fmt.Sprintf("\nWaiting for restore task %s on universe %s(%s) to be completed\n",
					formatter.Colorize(taskUUID, formatter.GreenColor), universeNameFlag, universeUUID))
				err = authAPI.WaitForTask(taskUUID, msg)
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}

				var limit int32 = 10
				var offset int32 = 0
				restoreAPIDirection := "DESC"
				restoreAPISort := "createTime"

				universeUUIDList := make([]string, 0)
				if len(strings.TrimSpace(universeUUID)) > 0 {
					universeUUIDList = append(universeUUIDList, universeUUID)
				}

				restoreAPIFilter := ybaclient.RestoreApiFilter{
					UniverseUUIDList: universeUUIDList,
				}

				restoreAPIQuery := ybaclient.RestorePagedApiQuery{
					Filter:    restoreAPIFilter,
					Direction: restoreAPIDirection,
					Limit:     limit,
					Offset:    offset,
					SortBy:    restoreAPISort,
				}

				restoreListRequest := authAPI.ListRestores().PageRestoresRequest(restoreAPIQuery)

				// Execute restore list request
				r, response, err := restoreListRequest.Execute()
				if err != nil {
					errMessage := util.ErrorFromHTTPResponse(
						response, err, "Restore", "Create - Get Restore")
					logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
				}

				restoreCtx := formatter.Context{
					Command: "create",
					Output:  os.Stdout,
					Format:  restore.NewRestoreFormat(viper.GetString("output")),
				}
				restore.Write(restoreCtx, r.GetEntities())
			}
		} else {
			logrus.Infoln(msg + "\n")
		}

	},
}

func buildBackupInfoList(backupInfos []string) (res []ybaclient.BackupStorageInfo) {

	for _, backupInfo := range backupInfos {
		backupDetails := map[string]string{}
		for _, keyspaceInfo := range strings.Split(backupInfo, ";") {
			kvp := strings.Split(keyspaceInfo, "=")
			if len(kvp) != 2 {
				logrus.Fatalln(
					formatter.Colorize("Incorrect format in keyspace description.",
						formatter.RedColor))
			}
			key := kvp[0]
			val := kvp[1]
			switch key {
			case "keyspace-name":
				if len(strings.TrimSpace(val)) != 0 {
					backupDetails["keyspace-name"] = val
				}
			case "storage-location":
				if len(strings.TrimSpace(val)) != 0 {
					backupDetails["storage-location"] = val
				}
			case "backup-type":
				if !strings.EqualFold(val, "ysql") && !strings.EqualFold(val, "ycql") && !strings.EqualFold(val, "yedis") {
					logrus.Fatalf(fmt.Sprintf("Table type provided %s is not supported", val))
				}

				var backupType string
				if strings.EqualFold(val, "ysql") {
					backupType = util.PgSqlTableType
				} else if strings.EqualFold(val, "ycql") {
					backupType = util.YqlTableType
				} else {
					backupType = util.RedisTableType
				}
				if len(strings.TrimSpace(val)) != 0 {
					backupDetails["backup-type"] = backupType
				}
			case "use-tablespaces":
				backupDetails["use-tablespaces"] = "false"
				if len(strings.TrimSpace(val)) != 0 {
					if strings.EqualFold(val, "true") {
						backupDetails["use-tablespaces"] = "true"
					}
				}
			case "selective-restore":
				backupDetails["selective-restore"] = "false"
				if len(strings.TrimSpace(val)) != 0 {
					if strings.EqualFold(val, "true") {
						backupDetails["selective-restore"] = "true"
					}
				}
			case "table-name-list":
				backupDetails["table-name-list"] = ""
				if len(strings.TrimSpace(val)) != 0 {
					backupDetails["table-name-list"] = val
				}
			}
		}

		isSelectiveTableRestore, _ := strconv.ParseBool(backupDetails["selective-restore"])
		tableNameList := []string{}
		if backupDetails["table-names"] != "" {
			tableNameList = strings.Split(backupDetails["table-name-list"], ",")
		}

		r := ybaclient.BackupStorageInfo{
			BackupType:            util.GetStringPointer(backupDetails["backup-type"]),
			Keyspace:              util.GetStringPointer(backupDetails["keyspace-name"]),
			StorageLocation:       util.GetStringPointer(backupDetails["storage-location"]),
			Sse:                   util.GetBoolPointer(true),
			SelectiveTableRestore: util.GetBoolPointer(isSelectiveTableRestore),
			TableNameList:         &tableNameList,
		}
		res = append(res, r)
	}
	return res
}

func init() {
	createBackupRestoreCmd.Flags().SortFlags = false
	createBackupRestoreCmd.Flags().String("universe-name", "",
		"[Required] Universe name. Name of the universe to be backed up")
	createBackupRestoreCmd.MarkFlagRequired("universe-name")
	createBackupRestoreCmd.Flags().String("storage-config-name", "",
		"[Required] Storage config to be used for taking the backup")
	createBackupRestoreCmd.MarkFlagRequired("storage-config-name")
	createBackupRestoreCmd.Flags().StringArray("keyspace-info", []string{},
		"[Required] Keyspace info to perform restore operation."+
			" Provide the following semicolon separated fields as key value pairs, and"+
			" enclose the string with quotes: "+
			"\"'keyspace-name=<keyspace-name>;storage-location=<storage_location>;"+
			"backup-type=<ycql/ysql>;use-tablespaces=<use-tablespaces>;"+
			"selective-restore=<selective-restore>;table-name-list=<table-name1>,<table-name2>'\"."+
			" The table-name-list attribute has to be specified as comma separated values. "+
			formatter.Colorize("Keyspace name, storage-location and backup-type are required "+
				"values. ", formatter.GreenColor)+
			"The attribute use-tablespaces, selective-restore and table-name-list are optional values. "+
			"Attributes selective-restore and table-name-list and are needed only for YCQL. "+
			"The attribute use-tablespaces is to be used if needed only in the case of YSQL"+
			"Example: --keyspace-info 'keyspace-name=cassandra1;storage-location=s3://bucket/location1;"+
			"backup-type=ycql;selective-restore=true;table-name-list=table1,table2' "+
			"--keyspace-info 'keyspace-name=postgres;storage-location=s3://bucket/location2"+
			"backup-type=ysql;use-tablespaces=true'")
	createBackupRestoreCmd.MarkFlagRequired("keyspace-info")

}
