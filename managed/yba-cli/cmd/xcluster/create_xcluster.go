/*
 * Copyright (c) YugaByte, Inc.
 */

package xcluster

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
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/xcluster"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/ybatask"
)

var createXClusterCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add"},
	Short:   "Create an asynchronous replication config in YugabyteDB Anywhere",
	Long:    "Create an asynchronous replication config in YugabyteDB Anywhere",
	Example: `yba xcluster create --name <xcluster-name> \
	--source-universe-name <source-universe-name> \
	--target-universe-name <target-universe-name> \
	--table-uuids <uuid-1>,<uuid-2>,<uuid-3> \
	--storage-config-name <storage-config-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(name) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize(
					"No xcluster name found to create replication\n",
					formatter.RedColor,
				),
			)
		}

		storageConfigNameFlag, err := cmd.Flags().GetString("storage-config-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(storageConfigNameFlag)) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize(
					"No storage config name found to take a backup\n",
					formatter.RedColor,
				),
			)
		}

		sourceUniName, err := cmd.Flags().GetString("source-universe-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		targetUniName, err := cmd.Flags().GetString("target-universe-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(sourceUniName)) == 0 ||
			len(strings.TrimSpace(targetUniName)) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("Missing source or target universe name\n", formatter.RedColor))
		}

		tableUUIDs, err := cmd.Flags().GetString("table-uuids")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(strings.TrimSpace(tableUUIDs)) == 0 {
			tableType, err := cmd.Flags().GetString("table-type")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if len(strings.TrimSpace(tableType)) == 0 {
				cmd.Help()
				logrus.Fatalln(
					formatter.Colorize(
						"Table type not specified when table-uuids is missing\n",
						formatter.RedColor))
			}
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		sourceUniverseName, err := cmd.Flags().GetString("source-universe-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		targetUniverseName, err := cmd.Flags().GetString("target-universe-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		sourceUniverse, targetUniverse := GetSourceAndTargetXClusterUniverse(
			authAPI, sourceUniverseName, targetUniverseName, "", "", "Create")

		sourceUniverseUUID := sourceUniverse.GetUniverseUUID()

		targetUniverseUUID := targetUniverse.GetUniverseUUID()

		dryRun, err := cmd.Flags().GetBool("dry-run")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		tableUUIDsString, err := cmd.Flags().GetString("table-uuids")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		tableNeedBootstrapUUIDsString, err := cmd.Flags().GetString("tables-need-full-copy-uuids")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		parallelism, err := cmd.Flags().GetInt("parallelism")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		storageConfigName, err := cmd.Flags().GetString("storage-config-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		allowBootstrap, err := cmd.Flags().GetBool("allow-bootstrap")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		configType, err := cmd.Flags().GetString("config-type")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		configType = strings.ToLower(configType)
		switch configType {
		case "basic":
			configType = util.BasicXClusterConfigType
		case "txn":
			configType = util.TxnXClusterConfigType
		case "db":
			configType = util.DBXClusterConfigType
		default:
			configType = util.BasicXClusterConfigType
		}

		storageConfigListRequest := authAPI.GetListOfCustomerConfig()
		rStorageConfigList, response, err := storageConfigListRequest.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(
				response, err, "Backup", "Create - Get Storage Configuration")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		storageConfigs := make([]ybaclient.CustomerConfigUI, 0)
		for _, s := range rStorageConfigList {
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
		rStorageConfigList = storageConfigsName

		if len(rStorageConfigList) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No storage configurations with name: %s found\n",
						storageConfigName),
					formatter.RedColor,
				))
			return
		}

		var storageUUID string
		if len(rStorageConfigList) > 0 {
			storageUUID = rStorageConfigList[0].GetConfigUUID()
		}

		tableUUIDsString = strings.TrimSpace(tableUUIDsString)
		tableUUIDs := make([]string, 0)
		if len(tableUUIDsString) != 0 {
			tableUUIDs = strings.Split(tableUUIDsString, ",")
		} else {
			rTables, response, err := authAPI.
				GetAllTables(sourceUniverseUUID).IncludeParentTableInfo(false).
				IncludeColocatedParentTables(true).
				XClusterSupportedOnly(true).Execute()
			if err != nil {
				errMessage := util.ErrorFromHTTPResponse(
					response,
					err,
					"XCluster",
					"Create - List Tables")
				logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
			}

			tableTypeFlag, err := cmd.Flags().GetString("table-type")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}

			tableType := ""
			switch strings.ToUpper(tableTypeFlag) {
			case util.YSQLWorkloadType:
				tableType = util.PgSqlTableType
			case util.YCQLWorkloadType:
				tableType = util.YqlTableType
			}

			for _, table := range rTables {
				if strings.Compare(table.GetTableType(), tableType) == 0 {
					tableUUIDs = append(tableUUIDs, table.GetTableUUID())
				}
			}
		}

		tableNeedBootstrapUUIDsString = strings.TrimSpace(tableNeedBootstrapUUIDsString)
		tableNeedBootstrapUUIDs := make([]string, 0)
		if len(tableNeedBootstrapUUIDsString) != 0 {
			tableNeedBootstrapUUIDs = strings.Split(tableNeedBootstrapUUIDsString, ",")
		} else {
			allowBootstrap = true
		}

		req := ybaclient.XClusterConfigCreateFormData{
			DryRun:             util.GetBoolPointer(dryRun),
			Tables:             tableUUIDs,
			Name:               name,
			SourceUniverseUUID: sourceUniverseUUID,
			TargetUniverseUUID: targetUniverseUUID,
			ConfigType:         util.GetStringPointer(configType),
			BootstrapParams: &ybaclient.BootstrapParams{
				BackupRequestParams: ybaclient.BootstrapBackupParams{
					StorageConfigUUID: storageUUID,
					Parallelism:       util.GetInt32Pointer(int32(parallelism)),
				},
				Tables:         tableNeedBootstrapUUIDs,
				AllowBootstrap: util.GetBoolPointer(allowBootstrap),
			},
		}

		rTask, response, err := authAPI.CreateXClusterConfig().
			XclusterReplicationCreateFormData(req).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "XCluster", "Create")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		msg := fmt.Sprintf(
			"The xcluster config %s between source universe %s (%s) "+
				"and target universe %s (%s) is being created",
			formatter.Colorize(name, formatter.GreenColor),
			sourceUniverse.GetName(),
			sourceUniverseUUID,
			targetUniverse.GetName(),
			targetUniverseUUID,
		)

		if viper.GetBool("wait") {
			if len(rTask.GetTaskUUID()) > 0 {
				logrus.Info(fmt.Sprintf("Waiting for xcluster %s to be created\n",
					formatter.Colorize(name, formatter.GreenColor)))
				err = authAPI.WaitForTask(rTask.GetTaskUUID(), msg)
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
			}
			logrus.Infof(
				"The xcluster config %s between source universe %s (%s) "+
					"and target universe %s (%s) has been created\n",
				formatter.Colorize(name, formatter.GreenColor),
				sourceUniverse.GetName(),
				sourceUniverseUUID,
				targetUniverse.GetName(),
				targetUniverseUUID)

			uuid := rTask.GetResourceUUID()

			rXCluster, response, err := authAPI.GetXClusterConfig(uuid).Execute()
			if err != nil {
				errMessage := util.ErrorFromHTTPResponse(
					response,
					err,
					"XCluster",
					"Create - Get XCluster")
				logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
			}
			r := make([]ybaclient.XClusterConfigGetResp, 0)
			r = append(r, rXCluster)

			xcluster.SourceUniverse = sourceUniverse
			xcluster.TargetUniverse = targetUniverse

			xclusterCtx := formatter.Context{
				Command: "create",
				Output:  os.Stdout,
				Format:  xcluster.NewXClusterFormat(viper.GetString("output")),
			}
			xcluster.Write(xclusterCtx, r)

			return
		}
		logrus.Infoln(msg + "\n")
		taskCtx := formatter.Context{
			Command: "create",
			Output:  os.Stdout,
			Format:  ybatask.NewTaskFormat(viper.GetString("output")),
		}
		ybatask.Write(taskCtx, []ybaclient.YBPTask{rTask})

	},
}

func init() {
	createXClusterCmd.Flags().SortFlags = false

	createXClusterCmd.Flags().StringP("name", "n", "",
		"[Required] Name of the xcluster config to create. "+
			"The name of the replication config cannot contain "+
			"[SPACE '_' '*' '<' '>' '?' '|' '\"' NULL] characters.")
	createXClusterCmd.MarkFlagRequired("name")

	createXClusterCmd.Flags().String("source-universe-name", "",
		"[Required] The name of the source universe for the xcluster config.")
	createXClusterCmd.MarkFlagRequired("source-universe-name")

	createXClusterCmd.Flags().String("target-universe-name", "",
		"[Required] The name of the target universe for the xcluster config.")
	createXClusterCmd.MarkFlagRequired("target-universe-name")

	createXClusterCmd.Flags().String("table-type", "",
		fmt.Sprintf("[Optional] Table type. %s. Allowed values: ysql, ycql",
			formatter.Colorize("Required when table-uuids is not specified",
				formatter.GreenColor)))

	createXClusterCmd.Flags().String("table-uuids", "",
		"[Optional] Comma separated list of source universe table IDs/UUIDs. "+
			"All tables must be of the same type. "+
			"Run \"yba universe table list --name <source-universe-name> --xcluster-supported-only\""+
			" to check the list of tables that can be added for asynchronous replication. If left empty, "+
			"all tables of specified table-type will be added for asynchronous replication.")

	createXClusterCmd.Flags().String("storage-config-name", "",
		"[Required] Storage config to be used for taking the backup for replication. ")
	createXClusterCmd.MarkFlagRequired("storage-config-name")

	createXClusterCmd.Flags().String("tables-need-full-copy-uuids", "",
		"[Optional] Comma separated list of source universe table IDs/UUIDs that are allowed to be "+
			"full-copied to the target universe. Must be a subset of table-uuids. If left empty,"+
			" allow-bootstrap is set to true so full-copy can be done for all the tables passed "+
			"in to be in replication. Run \"yba xcluster needs-full-copy-tables --source-universe-name"+
			" <source-universe-name> --target-universe-name <target-universe-name> --table-uuids"+
			" <tables-from-table-uuids-flag>\" to check the list of tables that need bootstrapping.")

	createXClusterCmd.Flags().Int("parallelism", 8,
		"[Optional] Number of concurrent commands to run on nodes over SSH via \"yb_backup\" script.")

	createXClusterCmd.Flags().Bool("allow-bootstrap", false,
		"Allow full copy on all the tables being added to the replication. "+
			"The same as passing the same set passed to table-uuids to "+
			"tables-need-full-copy-uuids. (default false)")

	createXClusterCmd.Flags().String("config-type", "basic",
		"[Optional] Scope of the xcluster config to create. "+
			"Allowed values: basic, txn, db.")

	createXClusterCmd.Flags().Bool("dry-run", false,
		"[Optional] Run the pre-checks without actually running the subtasks. (default false)")

}
