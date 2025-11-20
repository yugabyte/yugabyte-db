/*
 * Copyright (c) YugabyteDB, Inc.
 */

package restore

import (
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
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/ybatask"
)

// createRestoreCmd represents the universe backup command
var createRestoreCmd = &cobra.Command{
	Use:   "create",
	Short: "Restore a YugabyteDB Anywhere universe backup",
	Long:  "Restore an universe backup in YugabyteDB Anywhere",
	Example: `yba backup restore create --universe-name <universe-name> \
	--storage-config-name <storage-config-name> \
	--keyspace-info \
	keyspace-name=<keyspace-name-1>::storage-location=<storage-location-1>::backup-type=ysql \
	--keyspace-info \
	keyspace-name=<keyspace-name-2>::storage-location=<storage-location-2>::backup-type=ysql`,
	PreRun: func(cmd *cobra.Command, args []string) {
		universeNameFlag, err := cmd.Flags().GetString("universe-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(universeNameFlag) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize(
					"No universe name found to restore backup to\n",
					formatter.RedColor,
				),
			)
		}

		storageConfigNameFlag, err := cmd.Flags().GetString("storage-config-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(storageConfigNameFlag) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize(
					"No storage config name found for restore\n",
					formatter.RedColor,
				),
			)
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		var response *http.Response
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		universeNameFlag, err := cmd.Flags().GetString("universe-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		universeListRequest := authAPI.ListUniverses()
		universeListRequest = universeListRequest.Name(universeNameFlag)

		r, response, err := universeListRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Restore", "Create - Get Universe")
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
			util.FatalHTTPError(response, err, "Restore", "Create - Get Storage Configuration")
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
			logrus.Fatalf(
				formatter.Colorize(
					"No storage configurations with name "+storageName+" found",
					formatter.RedColor,
				),
			)
		}

		var storageUUID string
		if len(rList) > 0 {
			storageUUID = rList[0].GetConfigUUID()
		}

		kmsConfigUUID := ""
		kmsConfigName, err := cmd.Flags().GetString("kms-config")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		// find kmsConfigUUID from the name
		kmsConfigs, response, err := authAPI.ListKMSConfigs().Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Restore", "Create - Fetch KMS Configs")
		}
		for _, k := range kmsConfigs {
			metadataInterface := k["metadata"]
			if metadataInterface != nil {
				metadata := metadataInterface.(map[string]interface{})
				kmsName := metadata["name"]
				if kmsName != nil && strings.Compare(kmsName.(string), kmsConfigName) == 0 {
					configUUID := metadata["configUUID"]
					if configUUID != nil {
						kmsConfigUUID = configUUID.(string)
					}
				}
			}
		}
		if !util.IsEmptyString(kmsConfigName) &&
			util.IsEmptyString(kmsConfigUUID) {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No KMS configuration with name: %s found\n",
						kmsConfigName),
					formatter.RedColor,
				))
		}

		enableVerboseLogs, err := cmd.Flags().GetBool("enable-verbose-logs")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		parallelism, err := cmd.Flags().GetInt("parallelism")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		backupInfos, err := cmd.Flags().GetStringArray("keyspace-info")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		var result []ybaclient.BackupStorageInfo = buildBackupInfoList(backupInfos)
		requestBody := ybaclient.RestoreBackupParams{
			UniverseUUID:          universeUUID,
			CustomerUUID:          util.GetStringPointer(authAPI.CustomerUUID),
			StorageConfigUUID:     util.GetStringPointer(storageUUID),
			BackupStorageInfoList: result,
			KmsConfigUUID:         util.GetStringPointer(kmsConfigUUID),
			EnableVerboseLogs:     util.GetBoolPointer(enableVerboseLogs),
			Parallelism:           util.GetInt32Pointer(int32(parallelism)),
		}

		rTask, response, err := authAPI.RestoreBackup().Backup(requestBody).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Restore", "Create")
		}

		task := util.CheckTaskAfterCreation(rTask)

		taskUUID := task.GetTaskUUID()
		msg := fmt.Sprintf("The restore task %s is in progress",
			formatter.Colorize(taskUUID, formatter.GreenColor))

		if viper.GetBool("wait") {
			if taskUUID != "" {
				logrus.Info(
					fmt.Sprintf(
						"\nWaiting for restore task %s on universe %s (%s) to be completed\n",
						formatter.Colorize(
							taskUUID,
							formatter.GreenColor,
						),
						universeNameFlag,
						universeUUID,
					),
				)
				err = authAPI.WaitForTask(taskUUID, msg)
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
			}
			var limit int32 = 10
			var offset int32 = 0
			restoreAPIDirection := util.DescSortDirection
			restoreAPISort := "createTime"

			universeUUIDList := make([]string, 0)
			if !util.IsEmptyString(universeUUID) {
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
				util.FatalHTTPError(response, err, "Restore", "Create - Get Restore")
			}

			restoreCtx := formatter.Context{
				Command: "list",
				Output:  os.Stdout,
				Format:  restore.NewRestoreFormat(viper.GetString("output")),
			}
			restore.Write(restoreCtx, r.GetEntities())
			return
		}
		logrus.Infoln(msg + "\n")
		taskCtx := formatter.Context{
			Command: "create",
			Output:  os.Stdout,
			Format:  ybatask.NewTaskFormat(viper.GetString("output")),
		}
		ybatask.Write(taskCtx, []ybaclient.YBPTask{task})

	},
}

func buildBackupInfoList(backupInfos []string) (res []ybaclient.BackupStorageInfo) {

	for _, backupInfo := range backupInfos {
		backupDetails := map[string]string{}
		for _, keyspaceInfo := range strings.Split(backupInfo, util.Separator) {
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
				if !util.IsEmptyString(val) {
					backupDetails["keyspace-name"] = val
				}
			case "storage-location":
				if !util.IsEmptyString(val) {
					backupDetails["storage-location"] = val
				}
			case "backup-type":
				if !strings.EqualFold(val, "ysql") && !strings.EqualFold(val, "ycql") &&
					!strings.EqualFold(val, "yedis") {
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
				if !util.IsEmptyString(val) {
					backupDetails["backup-type"] = backupType
				}
			case "use-tablespaces":
				backupDetails["use-tablespaces"] = "false"
				if !util.IsEmptyString(val) {
					if strings.EqualFold(val, "true") {
						backupDetails["use-tablespaces"] = "true"
					}
				}
			case "selective-restore":
				backupDetails["selective-restore"] = "false"
				if !util.IsEmptyString(val) {
					if strings.EqualFold(val, "true") {
						backupDetails["selective-restore"] = "true"
					}
				}
			case "table-name-list":
				backupDetails["table-name-list"] = ""
				if !util.IsEmptyString(val) {
					backupDetails["table-name-list"] = val
				}
			}
		}

		useTablespaces, err := strconv.ParseBool(backupDetails["use-tablespaces"])
		if err != nil {
			errMessage := err.Error() +
				" Invalid or missing value provided for 'use-tablespaces'. Setting it to 'false'.\n"
			logrus.Errorln(
				formatter.Colorize(errMessage, formatter.YellowColor),
			)
			useTablespaces = false
		}

		isSelectiveTableRestore, err := strconv.ParseBool(backupDetails["selective-restore"])
		if err != nil {
			errMessage := err.Error() +
				" Invalid or missing value provided for 'selective-restore'. Setting it to 'false'.\n"
			logrus.Errorln(
				formatter.Colorize(errMessage, formatter.YellowColor),
			)
			isSelectiveTableRestore = false
		}
		tableNameList := []string{}
		if backupDetails["table-name-list"] != "" {
			tableNameList = strings.Split(backupDetails["table-name-list"], ",")
		}

		r := ybaclient.BackupStorageInfo{
			BackupType:            util.GetStringPointer(backupDetails["backup-type"]),
			Keyspace:              util.GetStringPointer(backupDetails["keyspace-name"]),
			StorageLocation:       util.GetStringPointer(backupDetails["storage-location"]),
			Sse:                   util.GetBoolPointer(true),
			SelectiveTableRestore: util.GetBoolPointer(isSelectiveTableRestore),
			UseTablespaces:        util.GetBoolPointer(useTablespaces),
			TableNameList:         tableNameList,
		}
		res = append(res, r)
	}
	return res
}

func init() {
	createRestoreCmd.Flags().SortFlags = false
	createRestoreCmd.Flags().String("universe-name", "",
		"[Required] Target universe name to perform restore operation.")
	createRestoreCmd.MarkFlagRequired("universe-name")
	createRestoreCmd.Flags().String("storage-config-name", "",
		"[Required] Storage config to be used for taking the backup")
	createRestoreCmd.MarkFlagRequired("storage-config-name")
	createRestoreCmd.Flags().String("kms-config", "",
		"[Optional] Key management service config name. "+
			"For a successful restore, the KMS configuration used for restore should be the same "+
			"KMS configuration used during backup creation.")
	createRestoreCmd.Flags().StringArray("keyspace-info", []string{},
		"[Required] Keyspace info to perform restore operation."+
			" Provide the following double colon (::) separated fields as key value pairs: "+
			"\"keyspace-name=<keyspace-name>::storage-location=<storage_location>::"+
			"backup-type=<ycql/ysql>::use-tablespaces=<use-tablespaces>::"+
			"selective-restore=<selective-restore>::table-name-list=<table-name1>,<table-name2>\"."+
			" The table-name-list attribute has to be specified as comma separated values. "+
			formatter.Colorize("Keyspace name, storage-location and backup-type are required "+
				"values. ", formatter.GreenColor)+
			"The attributes use-tablespaces, selective-restore and table-name-list are optional. "+
			"Attributes selective-restore and table-name-list are needed only for YCQL. "+
			"The attribute use-tablespaces is needed only for YSQL. "+
			"Example: --keyspace-info keyspace-name=cassandra1::storage-location=s3://bucket/location1::"+
			"backup-type=ycql::selective-restore=true::table-name-list=table1,table2 "+
			"--keyspace-info keyspace-name=postgres::storage-location=s3://bucket/location2"+
			"backup-type=ysql::use-tablespaces=true")
	createRestoreCmd.MarkFlagRequired("keyspace-info")
	createRestoreCmd.Flags().Bool("enable-verbose-logs", false,
		"[Optional] Enable verbose logging while taking backup via \"yb_backup\" script. (default false)")
	createRestoreCmd.Flags().Int("parallelism", 8,
		"[Optional] Number of concurrent commands to run on nodes over SSH via \"yb_backup\" script.")

}
