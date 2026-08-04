/*
 * Copyright (c) YugabyteDB, Inc.
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
	"golang.org/x/exp/slices"
)

var updateXClusterCmd = &cobra.Command{
	Use:     "update",
	Aliases: []string{"edit"},
	Short:   "Update an asynchronous replication config in YugabyteDB Anywhere",
	Long:    "Update an asynchronous replication config in YugabyteDB Anywhere",
	Example: `yba xcluster update --uuid <uuid> \
	 --add-table-uuids <uuid-1>,<uuid-2>,<uuid-3> \
	 --storage-config-name <storage-config-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		uuid, er := cmd.Flags().GetString("uuid")
		if er != nil {
			logrus.Fatalf(formatter.Colorize(er.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(uuid) {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No xcluster uuid found to update\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		uuid, er := cmd.Flags().GetString("uuid")
		if er != nil {
			logrus.Fatalf(formatter.Colorize(er.Error()+"\n", formatter.RedColor))
		}

		rXCluster, response, err := authAPI.GetXClusterConfig(uuid).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "xCluster", "Update - Get xCluster")
		}
		validStatesForConfig := util.TableStatesInXClusterConfig()
		xClusterTables := make([]string, 0)
		tableDetails := rXCluster.GetTableDetails()
		for _, table := range tableDetails {
			if slices.Contains(validStatesForConfig, table.GetStatus()) {
				xClusterTables = append(xClusterTables, table.GetTableId())
			}
		}

		name := rXCluster.GetName()

		dryRun, err := cmd.Flags().GetBool("dry-run")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		req := ybaclient.XClusterConfigEditFormData{
			DryRun: util.GetBoolPointer(dryRun),
		}

		if cmd.Flags().Changed("auto-include-index-tables") {
			autoIncludeIndexTables, err := cmd.Flags().GetBool("auto-include-index-tables")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if autoIncludeIndexTables {
				logrus.Debugf("Including index tables\n")
			}
			req.SetAutoIncludeIndexTables(autoIncludeIndexTables)
		}

		sourceRole, err := cmd.Flags().GetString("source-role")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(sourceRole) {
			logrus.Debugf("Updating source role\n")
			req.SetSourceRole(strings.ToUpper(sourceRole))
		}

		targetRole, err := cmd.Flags().GetString("target-role")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(targetRole) {
			logrus.Debugf("Updating target role\n")
			req.SetTargetRole(strings.ToUpper(targetRole))
		}

		addTableUUIDsString, err := cmd.Flags().GetString("add-table-uuids")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		removeTableUUIDsString, err := cmd.Flags().GetString("remove-table-uuids")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		formTableUUIDs := make([]string, 0)
		removeTableUUIDsString = strings.TrimSpace(removeTableUUIDsString)
		if len(removeTableUUIDsString) != 0 {
			// Making UUIDs to IDs for comparison
			removeTableUUIDsString = strings.ReplaceAll(removeTableUUIDsString, "-", "")
			removeTableUUIDs := strings.Split(removeTableUUIDsString, ",")
			for _, tableUUID := range xClusterTables {
				if !slices.Contains(removeTableUUIDs, tableUUID) {
					formTableUUIDs = append(formTableUUIDs, tableUUID)
				}
			}
			// In case add is empty, this list is sent for xCluster
			logrus.Debug("Removing tables from XCluster")
			req.SetTables(formTableUUIDs)
		} else {
			formTableUUIDs = xClusterTables
		}

		allowBoostrap := false

		skipBootstrap, err := cmd.Flags().GetBool("skip-full-copy-tables")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		addTableUUIDsString = strings.TrimSpace(addTableUUIDsString)
		addTableUUIDs := make([]string, 0)
		if len(addTableUUIDsString) != 0 {
			addTableUUIDs = strings.Split(addTableUUIDsString, ",")
			formTableUUIDs = append(formTableUUIDs, addTableUUIDs...)

			logrus.Debug("Adding tables to XCluster")
			req.SetTables(formTableUUIDs)

			if !skipBootstrap {
				tableNeedBootstrapUUIDsString, err := cmd.Flags().
					GetString("tables-need-full-copy-uuids")
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}

				tableNeedBootstrapUUIDsString = strings.TrimSpace(tableNeedBootstrapUUIDsString)
				tableNeedBootstrapUUIDs := make([]string, 0)
				if len(tableNeedBootstrapUUIDsString) != 0 {
					tableNeedBootstrapUUIDs = strings.Split(tableNeedBootstrapUUIDsString, ",")
				} else {
					tableNeedBootstrapUUIDs = addTableUUIDs
					allowBoostrap = true
				}

				if len(tableNeedBootstrapUUIDs) > 0 || allowBoostrap {
					logrus.Debug("Updating tables needing bootstrap\n")
					bootstrapParams := ybaclient.BootstrapParams{
						Tables: tableNeedBootstrapUUIDs,
					}

					if allowBoostrap {
						logrus.Debug("Updating allow bootstrap to true\n")
						bootstrapParams.SetAllowBootstrap(allowBoostrap)
					} else if cmd.Flags().Changed("allow-full-copy-tables") {
						allowBootstrap, err := cmd.Flags().GetBool("allow-full-copy-tables")
						if err != nil {
							logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
						}
						logrus.Debug("Updating allow bootstrap\n")
						bootstrapParams.SetAllowBootstrap(allowBootstrap)
					}

					backupBootstrapParams := ybaclient.BootstrapBackupParams{}

					parallelism, err := cmd.Flags().GetInt("parallelism")
					if err != nil {
						logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
					}
					logrus.Debug("Updating parallelism\n")
					backupBootstrapParams.SetParallelism(int32(parallelism))

					storageConfigName, err := cmd.Flags().GetString("storage-config-name")
					if err != nil {
						logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
					}

					if util.IsEmptyString(storageConfigName) {
						logrus.Fatalf(
							formatter.Colorize(
								"Storage configuration must be provided since a tables need bootstrap\n",
								formatter.RedColor,
							),
						)
					}

					storageConfigListRequest := authAPI.GetListOfCustomerConfig()
					rStorageConfigList, response, err := storageConfigListRequest.Execute()
					if err != nil {
						errMessage := util.ErrorFromHTTPResponse(
							response, err, "Backup", "Update - Get Storage Configuration")
						logrus.Fatalf(
							formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor),
						)
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
					if len(storageUUID) != 0 {
						logrus.Debugf("Adding storage config for bootstrap\n")
						backupBootstrapParams.SetStorageConfigUUID(storageUUID)
					}

					bootstrapParams.SetBackupRequestParams(backupBootstrapParams)
					req.SetBootstrapParams(bootstrapParams)
				}
			}
		}

		rTask, response, err := authAPI.EditXClusterConfig(uuid).
			XclusterReplicationEditFormData(req).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "xCluster", "Update")
		}

		util.CheckTaskAfterCreation(rTask)

		msg := fmt.Sprintf(
			"The xcluster config %s (%s) is being updated",
			formatter.Colorize(name, formatter.GreenColor), uuid)

		if viper.GetBool("wait") {
			if len(rTask.GetTaskUUID()) > 0 {
				logrus.Info(fmt.Sprintf("Waiting for xcluster %s (%s) to be updated\n",
					formatter.Colorize(name, formatter.GreenColor), uuid))
				err = authAPI.WaitForTask(rTask.GetTaskUUID(), msg)
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
			}
			logrus.Infof(
				"The xcluster config %s (%s) has been updated\n",
				formatter.Colorize(name, formatter.GreenColor), uuid)

			uuid := rTask.GetResourceUUID()

			rXCluster, response, err := authAPI.GetXClusterConfig(uuid).Execute()
			if err != nil {
				util.FatalHTTPError(response, err, "xCluster", "Update - Get xCluster")
			}

			xclusterConfig := util.CheckAndDereference(
				rXCluster,
				"No xcluster found with uuid "+uuid,
			)

			r := make([]ybaclient.XClusterConfigGetResp, 0)
			r = append(r, xclusterConfig)

			sourceUniverse, targetUniverse := GetSourceAndTargetXClusterUniverse(
				authAPI, "", "",
				xclusterConfig.GetSourceUniverseUUID(),
				xclusterConfig.GetTargetUniverseUUID(),
				"Update")

			xcluster.SourceUniverse = sourceUniverse
			xcluster.TargetUniverse = targetUniverse

			xclusterCtx := formatter.Context{
				Command: "update",
				Output:  os.Stdout,
				Format:  xcluster.NewXClusterFormat(viper.GetString("output")),
			}
			xcluster.Write(xclusterCtx, r)

			return
		}
		logrus.Infoln(msg + "\n")
		task := util.CheckTaskAfterCreation(rTask)
		taskCtx := formatter.Context{
			Command: "update",
			Output:  os.Stdout,
			Format:  ybatask.NewTaskFormat(viper.GetString("output")),
		}
		ybatask.Write(taskCtx, []ybaclient.YBPTask{task})

	},
}

func init() {
	updateXClusterCmd.Flags().SortFlags = false

	updateXClusterCmd.Flags().StringP("uuid", "u", "",
		"[Required] The uuid of the xcluster to update.")
	updateXClusterCmd.MarkFlagRequired("uuid")

	updateXClusterCmd.Flags().String("source-role", "",
		"[Optional] The role that the source universe should have in the xCluster config. "+
			"Allowed values: active, standby, unrecognized.")
	updateXClusterCmd.Flags().String("target-role", "",
		"[Optional] The role that the target universe should have in the xCluster config. "+
			"Allowed values: active, standby, unrecognized.")

	updateXClusterCmd.Flags().String("add-table-uuids", "",
		"[Optional] Comma separated list of source universe table IDs/UUIDs. "+
			"All tables must be of the same type. "+
			"Run \"yba universe table list --name <source-universe-name> --xcluster-supported-only\""+
			" to check the list of tables that can be added for asynchronous replication.")

	updateXClusterCmd.Flags().String("remove-table-uuids", "",
		"[Optional] Comma separated list of source universe table IDs/UUIDs. "+
			"Run \"yba xcluster describe --uuid <xcluster-uuid>\""+
			" to check the list of tables that can be removed from asynchronous replication.")

	updateXClusterCmd.Flags().Bool("skip-full-copy-tables", false,
		"[Optional] Skip taking a backup for replication. (default false)")

	updateXClusterCmd.Flags().String("storage-config-name", "",
		fmt.Sprintf(
			"[Optional] Storage config to be used for taking the backup for replication. %s",
			formatter.Colorize("Required when tables require bootstrapping. "+
				"Ignored when skip-full-copy-tables is set to true.",
				formatter.GreenColor)))

	updateXClusterCmd.Flags().String("tables-need-full-copy-uuids", "",
		"[Optional] Comma separated list of source universe table IDs/UUIDs "+
			"that are allowed to be full-copied"+
			" to the target universe. Must be a subset of table-uuids. "+
			"If left empty, allow-full-copy-tables is set to true so full-copy can be done for "+
			"all the tables passed in to be in replication. Run \"yba xcluster needs-full-copy-tables"+
			" --source-universe-name <source-universe-name> --target-universe-name <target-universe-name> "+
			"--table-uuids <tables-from-table-uuids-flag>\" to check the list of tables that need "+
			"bootstrapping. Ignored when skip-full-copy-tables is set to true.")

	updateXClusterCmd.Flags().Bool("allow-full-copy-tables", false,
		"[Optional] Allow full copy on all the tables being added to the replication. "+
			"The same as passing the same set passed to table-uuids to tables-need-full-copy-uuids. "+
			"Ignored when skip-full-copy-tables is set to true."+
			" (default false)")

	updateXClusterCmd.Flags().Int("parallelism", 8,
		"[Optional] Number of concurrent commands to run on nodes over SSH via \"yb_backup\" script. "+
			"Ignored when skip-full-copy-tables is set to true.")

	updateXClusterCmd.Flags().Bool("dry-run", false,
		"[Optional] Run the pre-checks without actually running the subtasks. (default false)")

	updateXClusterCmd.Flags().Bool("auto-include-index-tables", false,
		"[Optional] Whether or not YBA should also include all index tables "+
			"from any provided main tables. (default false)")

}
