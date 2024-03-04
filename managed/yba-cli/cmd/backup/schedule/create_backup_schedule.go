/*
 * Copyright (c) YugaByte, Inc.
 */

package schedule

import (
	"fmt"
	"net/http"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// createBackupScheduleCmd represents the universe backup schedule command
var createBackupScheduleCmd = &cobra.Command{
	Use:   "create",
	Short: "Create a YugabyteDB Anywhere universe backup schedule",
	Long:  "Create an universe backup schedule in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		scheduleNameFlag, err := cmd.Flags().GetString("schedule-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(scheduleNameFlag)) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No schedule name specified to create a backup schedule\n", formatter.RedColor))
		}

		universeNameFlag, err := cmd.Flags().GetString("universe-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(universeNameFlag)) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to create a backup schedule\n", formatter.RedColor))
		}

		storageConfigNameFlag, err := cmd.Flags().GetString("storage-config-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(storageConfigNameFlag)) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No storage config name found to create a backup schedule\n", formatter.RedColor))
		}

		tableTypeFlag, err := cmd.Flags().GetString("table-type")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(tableTypeFlag)) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("Table type not specified to create a backup schedule\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		var response *http.Response
		authAPI, err := ybaAuthClient.NewAuthAPIClient()
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		authAPI.GetCustomerUUID()

		scheduleNameFlag, err := cmd.Flags().GetString("schedule-name")

		universeNameFlag, err := cmd.Flags().GetString("universe-name")

		universeListRequest := authAPI.ListUniverses()
		universeListRequest = universeListRequest.Name(universeNameFlag)

		r, response, err := universeListRequest.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Universe", "List")
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
		storageConfigName, err := cmd.Flags().GetString("storage-config-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		storageConfigListRequest := authAPI.GetListOfCustomerConfig()
		rList, response, err := storageConfigListRequest.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response, err, "Storage Configuration", "Describe")
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
		}

		tableTypeFlag, err := cmd.Flags().GetString("table-type")
		if !strings.EqualFold(tableTypeFlag, "ysql") && !strings.EqualFold(tableTypeFlag, "ycql") && !strings.EqualFold(tableTypeFlag, "yedis") {
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

		var keyspaceTableList []ybaclient.KeyspaceTable
		keyspaces, err := cmd.Flags().GetStringArray("keyspace-info")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		timeBeforeDeleteInMs, err := cmd.Flags().GetInt64("time-before-delete-in-ms")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		cronExpression, err := cmd.Flags().GetString("cron-expression")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		var scheduleFrequencyInSecs int64
		if len(strings.TrimSpace(cronExpression)) == 0 {
			var err error
			scheduleFrequencyInSecs, err = cmd.Flags().GetInt64("schedule-frequency-in-secs")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}

		if scheduleFrequencyInSecs == 0 && len(strings.TrimSpace(cronExpression)) == 0 {
			logrus.Fatalln(
				formatter.Colorize("Neither frequency of the schedule nor cron expression are provided",
					formatter.RedColor))
		}
		frequencyTimeUnit := "MINUTES"

		keyspaceTableList = buildKeyspaceTables(keyspaces)
		requestBody := ybaclient.BackupRequestParams{
			UniverseUUID:        universeUUID,
			CustomerUUID:        util.GetStringPointer(authAPI.CustomerUUID),
			ScheduleName:        util.GetStringPointer(scheduleNameFlag),
			StorageConfigUUID:   storageUUID,
			BackupType:          util.GetStringPointer(backupType),
			KeyspaceTableList:   &keyspaceTableList,
			UseTablespaces:      util.GetBoolPointer(useTablespaces),
			Sse:                 util.GetBoolPointer(sse),
			TimeBeforeDelete:    util.GetInt64Pointer(timeBeforeDeleteInMs),
			SchedulingFrequency: util.GetInt64Pointer(scheduleFrequencyInSecs * 1000),
			FrequencyTimeUnit:   util.GetStringPointer(frequencyTimeUnit),
			ExpiryTimeUnit:      util.GetStringPointer("MILLISECONDS"),
		}

		if (len(strings.TrimSpace(cronExpression))) > 0 {
			requestBody.SetCronExpression(cronExpression)
		}

		incrementalBackupFrequencyInSecs, err := cmd.Flags().GetInt64("incremental-backup-frequency-in-secs")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if incrementalBackupFrequencyInSecs > 0 {
			requestBody.SetIncrementalBackupFrequency(incrementalBackupFrequencyInSecs * 1000)
			requestBody.SetIncrementalBackupFrequencyTimeUnit("MINUTES")
		}

		rCreate, response, err := authAPI.CreateBackupSchedule().Backup(requestBody).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Universe", "Backup Schedule")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		taskUUID := rCreate.GetTaskUUID()
		msg := fmt.Sprintf("The backup schedule creation task %s is in progress",
			formatter.Colorize(taskUUID, formatter.GreenColor))

		if viper.GetBool("wait") {
			if taskUUID != "" {
				logrus.Info(fmt.Sprintf("\nWaiting for backup schedule creation task %s on universe (%s) to be completed\n",
					formatter.Colorize(taskUUID, formatter.GreenColor), universeNameFlag))
				err = authAPI.WaitForTask(taskUUID, msg)
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
				logrus.Infof("The backup schedule creation task %s is complete now\n", formatter.Colorize(taskUUID, formatter.GreenColor))
			}
		}

	},
}

func buildKeyspaceTables(keyspaces []string) (res []ybaclient.KeyspaceTable) {

	for _, keyspaceString := range keyspaces {
		keyspace := map[string]string{}
		for _, keyspaceInfo := range strings.Split(keyspaceString, ";") {
			kvp := strings.Split(keyspaceInfo, "=")
			if len(kvp) != 2 {
				logrus.Fatalln(
					formatter.Colorize("Incorrect format in region description.",
						formatter.RedColor))
			}
			key := kvp[0]
			val := kvp[1]
			switch key {
			case "keyspace-name":
				if len(strings.TrimSpace(val)) != 0 {
					keyspace["keyspace-name"] = val
				}
			case "table-names":
				if len(strings.TrimSpace(val)) != 0 {
					keyspace["table-names"] = val
				}
			case "table-ids":
				if len(strings.TrimSpace(val)) != 0 {
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
			TableNameList: &tableNameList,
			TableUUIDList: &tableIdList,
		}
		res = append(res, r)
	}
	return res
}

func init() {
	createBackupScheduleCmd.Flags().SortFlags = false
	createBackupScheduleCmd.Flags().String("schedule-name", "",
		"[Required] Schedule name. Name of the schedule to perform universe backups")
	createBackupScheduleCmd.MarkFlagRequired("schedule-name")
	createBackupScheduleCmd.Flags().String("cron-expression", "",
		"Cron expression to manage the backup schedules")
	createBackupScheduleCmd.Flags().Int64("schedule-frequency-in-secs", 0,
		"Backup frequency to manage the backup schedules")
	createBackupScheduleCmd.Flags().Int64("incremental-backup-frequency-in-secs", 0,
		"Incremental backup frequency to manage the incremental backup schedules")
	createBackupScheduleCmd.Flags().String("universe-name", "",
		"[Required] Universe name. Name of the universe to be backed up")
	createBackupScheduleCmd.MarkFlagRequired("universe-name")
	createBackupScheduleCmd.Flags().String("storage-config-name", "",
		"[Required] Storage config to be used for taking the backup")
	createBackupScheduleCmd.MarkFlagRequired("storage-config-name")
	createBackupScheduleCmd.Flags().String("table-type", "",
		"[Required] Table type. Allowed values: ysql, ycql, yedis")
	createBackupScheduleCmd.MarkFlagRequired("table-type")
	createBackupScheduleCmd.Flags().Int64("time-before-delete-in-ms", 0,
		"[Optional] Retention time of the backup in milliseconds")
	createBackupScheduleCmd.Flags().StringArray("keyspace-info", []string{},
		"[Optional] Keyspace info to perform backup operation."+
			"If no keyspace info is provided, then all the keyspaces of the table type "+
			"specified are backed up. If the user wants to take backup of a subset of keyspaces, "+
			"then the user has to specify the keyspace info. Provide the following semicolon "+
			"separated fields as key value pairs: "+
			"\"keyspace-name=<keyspace-name>;table-names=<table-name1>,<table-name2>,<table-name3>;"+
			"table-ids=<table-id1>,<table-id2>,<table-id3>\". The table-names and table-ids "+
			"attributes have to be specified as comma separated values."+
			formatter.Colorize("Keyspace name is required value. ", formatter.GreenColor)+
			"Table names and Table ids are optional values and are needed only for YCQL."+
			"Example: --keyspace-info keyspace-name=cassandra;table-names=table1,table2;"+
			"table-ids=1e683b86-7858-44d1-a1f6-406f50a4e56e,19a34a5e-3a19-4070-9d79-805ed713ce7d "+
			"--keyspace-info keyspace-name=cassandra2;table-names=table3,table4;"+
			"table-ids=e5b83a7c-130c-40c0-95ff-ec1d9ecff616,bc92d473-2e10-4f76-8bd1-9ca9741890fd")
	createBackupScheduleCmd.Flags().Bool("use-tablespaces", false,
		"[Optional] Backup tablespaces information as part of the backup")
	createBackupScheduleCmd.Flags().Bool("sse", true,
		"[Optional] Enable sse while persisting the data in AWS S3")

}
