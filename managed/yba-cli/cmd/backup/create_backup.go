/*
 * Copyright (c) YugabyteDB, Inc.
 */

package backup

import (
	"fmt"
	"net/http"
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
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/ybatask"
)

// createBackupCmd represents the universe backup command
var createBackupCmd = &cobra.Command{
	Use:   "create",
	Short: "Create a YugabyteDB Anywhere universe backup",
	Long:  "Create an universe backup in YugabyteDB Anywhere",
	Example: `yba backup create --universe-name <universe-name> \
	  --storage-config-name <storage-config-name> \
	  --table-type <table-type> \
	  --time-before-delete-in-ms 3600000 \
	  --keyspace-info keyspace-name=<keyspace-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		universeNameFlag, err := cmd.Flags().GetString("universe-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(universeNameFlag) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to take a backup\n", formatter.RedColor))
		}

		storageConfigNameFlag, err := cmd.Flags().GetString("storage-config-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(storageConfigNameFlag) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize(
					"No storage config name found to take a backup\n",
					formatter.RedColor,
				),
			)
		}

		tableTypeFlag, err := cmd.Flags().GetString("table-type")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(tableTypeFlag) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize(
					"Table type not specified to take a backup\n",
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
			util.FatalHTTPError(response, err, "Backup", "Create - Get Universe")
		}

		if len(r) < 1 {
			logrus.Fatalf(
				formatter.Colorize(fmt.Sprintf("Universe with name %s not found", universeNameFlag),
					formatter.RedColor))
		} else if len(r) > 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("Multiple universes with same name %s found", universeNameFlag),
					formatter.RedColor))
		}

		var universeUUID string
		if len(r) > 0 {
			universeUUID = r[0].GetUniverseUUID()
		}

		// filter by name
		storageConfigName, err := cmd.Flags().GetString("storage-config-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		storageConfigListRequest := authAPI.GetListOfCustomerConfig()
		rList, response, err := storageConfigListRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Backup", "Create - Get Storage Configuration")
		}

		storageConfigs := make([]ybaclient.CustomerConfigUI, 0)
		for _, s := range rList {
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
		rList = storageConfigsName

		if len(rList) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No storage configurations with name: %s found\n",
						storageConfigName),
					formatter.RedColor,
				))
			return
		}

		var storageUUID string
		if len(rList) > 0 {
			storageUUID = rList[0].GetConfigUUID()
			if len(backup.StorageConfigs) == 0 {
				backup.StorageConfigs = make([]ybaclient.CustomerConfigUI, 0)
			}
			backup.StorageConfigs = append(backup.StorageConfigs, rList[0])

		}

		backup.KMSConfigs, err = authAPI.GetListOfKMSConfigs(
			"Backup", "Create - Get KMS Configurations")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		tableTypeFlag, err := cmd.Flags().GetString("table-type")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !strings.EqualFold(tableTypeFlag, "ysql") &&
			!strings.EqualFold(tableTypeFlag, "ycql") &&
			!strings.EqualFold(tableTypeFlag, "yedis") {
			logrus.Fatalf(fmt.Sprintf("Table type provided %s is not supported", tableTypeFlag))
		}

		var backupType string
		if strings.EqualFold(tableTypeFlag, "ysql") {
			backupType = util.PgSqlTableType
		} else if strings.EqualFold(tableTypeFlag, "ycql") {
			backupType = util.YqlTableType
		} else {
			backupType = util.RedisTableType
		}

		useTablespaces, err := cmd.Flags().GetBool("use-tablespaces")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		sse, err := cmd.Flags().GetBool("sse")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		enableVerboseLogs, err := cmd.Flags().GetBool("enable-verbose-logs")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		parallelism, err := cmd.Flags().GetInt("parallelism")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		var keyspaceTableList []ybaclient.KeyspaceTable
		keyspaces, err := cmd.Flags().GetStringArray("keyspace-info")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		timeBeforeDeleteInMs, err := cmd.Flags().GetInt64("time-before-delete-in-ms")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		category, err := cmd.Flags().GetString("category")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		baseBackupUUID, err := cmd.Flags().GetString("base-backup-uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		keyspaceTableList = buildKeyspaceTables(keyspaces)
		requestBody := ybaclient.BackupRequestParams{
			UniverseUUID:      universeUUID,
			CustomerUUID:      util.GetStringPointer(authAPI.CustomerUUID),
			StorageConfigUUID: storageUUID,
			BackupCategory:    util.GetStringPointer(strings.ToUpper(category)),
			BackupType:        util.GetStringPointer(backupType),
			KeyspaceTableList: keyspaceTableList,
			UseTablespaces:    util.GetBoolPointer(useTablespaces),
			Sse:               util.GetBoolPointer(sse),
			TimeBeforeDelete:  util.GetInt64Pointer(timeBeforeDeleteInMs),
			ExpiryTimeUnit:    util.GetStringPointer("MILLISECONDS"),
			EnableVerboseLogs: util.GetBoolPointer(enableVerboseLogs),
			Parallelism:       util.GetInt32Pointer(int32(parallelism)),
		}

		if !util.IsEmptyString(baseBackupUUID) {
			requestBody.SetBaseBackupUUID(baseBackupUUID)
		}

		rTask, response, err := authAPI.CreateBackup().Backup(requestBody).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Backup", "Create")
		}

		task := util.CheckTaskAfterCreation(rTask)

		taskUUID := task.GetTaskUUID()
		msg := fmt.Sprintf("The backup task %s is in progress",
			formatter.Colorize(taskUUID, formatter.GreenColor))

		if viper.GetBool("wait") {
			if taskUUID != "" {
				logrus.Info(
					fmt.Sprintf(
						"\nWaiting for backup task %s on universe %s (%s) to be completed\n",
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
			backupTaskRequest := authAPI.GetBackupByTaskUUID(universeUUID, taskUUID)
			rBackup, response, err := backupTaskRequest.Execute()
			if err != nil {
				util.FatalHTTPError(response, err, "Backup", "Create - Get Backup")
			}
			backupUUID := rBackup[0].GetBackupUUID()
			baseBackupUUID := rBackup[0].GetBaseBackupUUID()

			backupUUIDList := make([]string, 0)
			if len(baseBackupUUID) != 0 {
				backupUUIDList = append(backupUUIDList, baseBackupUUID)
			} else {
				backupUUIDList = append(backupUUIDList, backupUUID)
			}

			var limit int32 = 10
			var offset int32 = 0
			backupAPIDirection := util.DescSortDirection
			backupAPISort := "createTime"

			backupAPIFilter := ybaclient.BackupApiFilter{
				BackupUUIDList: backupUUIDList,
			}

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
				util.FatalHTTPError(response, err, "Backup", "Create - Describe Backup")
			}

			if len(r.GetEntities()) < 1 {
				logrus.Fatalf(
					formatter.Colorize(
						fmt.Sprintf("No backups with UUID: %s found\n", backupUUID),
						formatter.RedColor,
					))
			}

			backupCtx := formatter.Context{
				Command: "create",
				Output:  os.Stdout,
				Format:  backup.NewBackupFormat(viper.GetString("output")),
			}
			backup.Write(backupCtx, r.GetEntities())

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

func buildKeyspaceTables(keyspaces []string) (res []ybaclient.KeyspaceTable) {

	for _, keyspaceString := range keyspaces {
		keyspace := map[string]string{}
		for _, keyspaceInfo := range strings.Split(keyspaceString, util.Separator) {
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
					keyspace["keyspace-name"] = val
				}
			case "table-names":
				if !util.IsEmptyString(val) {
					keyspace["table-names"] = val
				}
			case "table-ids":
				if !util.IsEmptyString(val) {
					keyspace["table-ids"] = val
				}
			}
		}
		tableNameList := []string{}
		if keyspace["table-names"] != "" {
			tableNameList = strings.Split(keyspace["table-names"], ",")
		}

		tableIdList := []string{}
		if keyspace["table-ids"] != "" {
			tableIdList = strings.Split(keyspace["table-ids"], ",")
		}

		r := ybaclient.KeyspaceTable{
			Keyspace:      util.GetStringPointer(keyspace["keyspace-name"]),
			TableNameList: tableNameList,
			TableUUIDList: tableIdList,
		}
		res = append(res, r)
	}
	return res
}

func init() {
	createBackupCmd.Flags().SortFlags = false
	createBackupCmd.Flags().String("universe-name", "",
		"[Required] Universe name. Name of the universe to be backed up")
	createBackupCmd.MarkFlagRequired("universe-name")
	createBackupCmd.Flags().String("storage-config-name", "",
		"[Required] Storage config to be used for taking the backup")
	createBackupCmd.MarkFlagRequired("storage-config-name")
	createBackupCmd.Flags().String("table-type", "",
		"[Required] Table type. Allowed values: ysql, ycql, yedis")
	createBackupCmd.MarkFlagRequired("table-type")
	createBackupCmd.Flags().Int64("time-before-delete-in-ms", 0,
		"[Optional] Retention time of the backup in milliseconds")
	createBackupCmd.Flags().StringArray("keyspace-info", []string{},
		"[Optional] Keyspace info to perform backup operation. "+
			"If no keyspace info is provided, then all the keyspaces of the table type "+
			"specified are backed up. If the user wants to take backup of a subset of keyspaces, "+
			"then the user has to specify the keyspace info. Provide the following double colon (::) "+
			"separated fields as key value pairs: "+
			"\"keyspace-name=<keyspace-name>::table-names=<table-name1>,<table-name2>,<table-name3>::"+
			"table-ids=<table-id1>,<table-id2>,<table-id3>\". The table-names and table-ids "+
			"attributes have to be specified as comma separated values. "+
			formatter.Colorize("Keyspace name is required value. ", formatter.GreenColor)+
			"Table names and Table IDs/UUIDs are optional values and are needed only for YCQL."+
			"Example: --keyspace-info keyspace-name=cassandra::table-names=table1,table2::"+
			"table-ids=1e683b86-7858-44d1-a1f6-406f50a4e56e,19a34a5e-3a19-4070-9d79-805ed713ce7d "+
			"--keyspace-info keyspace-name=cassandra2::table-names=table3,table4::"+
			"table-ids=e5b83a7c-130c-40c0-95ff-ec1d9ecff616,bc92d473-2e10-4f76-8bd1-9ca9741890fd")

	createBackupCmd.Flags().Bool("use-tablespaces", false,
		"[Optional] Backup tablespaces information as part of the backup")
	createBackupCmd.Flags().Bool("sse", true,
		"[Optional] Enable sse while persisting the data in AWS S3")
	createBackupCmd.Flags().String("category", "",
		"[Optional] Category of the backup. "+
			"If a universe has YBC enabled, then default value of category is YB_CONTROLLER. "+
			"Allowed values: yb_backup_script, yb_controller")
	createBackupCmd.Flags().Bool("enable-verbose-logs", false,
		"[Optional] Enable verbose logging while taking backup via \"yb_backup\" script. (default false)")
	createBackupCmd.Flags().Int("parallelism", 8,
		"[Optional] Number of concurrent commands to run on nodes over SSH via \"yb_backup\" script.")
	createBackupCmd.Flags().String("base-backup-uuid", "",
		"[Optional] Base Backup UUID for taking incremental backups")
}
