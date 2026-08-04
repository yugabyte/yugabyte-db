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
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/xcluster"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/ybatask"

	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

var restartXClusterCmd = &cobra.Command{
	Use:     "restart",
	Short:   "Restart replication for databases in the YugabyteDB Anywhere xCluster configuration",
	Long:    "Restart replication for databases in the YugabyteDB Anywhere xCluster configuration",
	Example: `yba xcluster restart --uuid <xcluster-uuid> --storage-config-name <storage-config-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		uuid, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(uuid) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize(
					"No xcluster uuid found to restart replication\n",
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
					"No storage config name found to take a backup\n",
					formatter.RedColor,
				),
			)
		}
		err = util.ConfirmCommand(
			fmt.Sprintf("Are you sure you want to restart %s: %s", "xcluster", uuid),
			viper.GetBool("force"))
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		uuid, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		dryRun, err := cmd.Flags().GetBool("dry-run")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		tableUUIDsString, err := cmd.Flags().GetString("table-uuids")
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

		storageConfigListRequest := authAPI.GetListOfCustomerConfig()
		rStorageConfigList, response, err := storageConfigListRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Backup", "Create - Get Storage Configuration")
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
		}

		req := ybaclient.XClusterConfigRestartFormData{
			DryRun: util.GetBoolPointer(dryRun),
			Tables: tableUUIDs,
			BootstrapParams: &ybaclient.RestartBootstrapParams{
				BackupRequestParams: ybaclient.BootstrapBackupParams{
					StorageConfigUUID: storageUUID,
					Parallelism:       util.GetInt32Pointer(int32(parallelism)),
				},
			},
		}

		forceDelete, err := cmd.Flags().GetBool("force-delete")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		rTask, response, err := authAPI.RestartXClusterConfig(uuid).
			XclusterReplicationRestartFormData(req).IsForceDelete(forceDelete).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "xCluster", "Restart")
		}

		util.CheckTaskAfterCreation(rTask)

		msg := fmt.Sprintf("The xcluster %s is being restarted",
			formatter.Colorize(uuid, formatter.GreenColor))

		if viper.GetBool("wait") {
			if len(rTask.GetTaskUUID()) > 0 {
				logrus.Info(fmt.Sprintf("Waiting for xcluster %s to be restarted\n",
					formatter.Colorize(uuid, formatter.GreenColor)))
				err = authAPI.WaitForTask(rTask.GetTaskUUID(), msg)
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
			}
			logrus.Infof("The xcluster %s has been restarted\n",
				formatter.Colorize(uuid, formatter.GreenColor))

			rXCluster, response, err := authAPI.GetXClusterConfig(uuid).Execute()
			if err != nil {
				util.FatalHTTPError(response, err, "xCluster", "Restart - Get xCluster")
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
				"Restart",
			)

			xcluster.SourceUniverse = sourceUniverse
			xcluster.TargetUniverse = targetUniverse

			xclusterCtx := formatter.Context{
				Command: "restart",
				Output:  os.Stdout,
				Format:  xcluster.NewXClusterFormat(viper.GetString("output")),
			}
			if len(r) < 1 {
				logrus.Fatalf(formatter.Colorize(
					fmt.Sprintf("No xcluster with uuid: %s found\n", uuid), formatter.RedColor))
			}
			xcluster.Write(xclusterCtx, r)
			return
		}
		logrus.Infoln(msg + "\n")
		task := util.CheckTaskAfterCreation(rTask)
		taskCtx := formatter.Context{
			Command: "restart",
			Output:  os.Stdout,
			Format:  ybatask.NewTaskFormat(viper.GetString("output")),
		}
		ybatask.Write(taskCtx, []ybaclient.YBPTask{task})

	},
}

func init() {
	restartXClusterCmd.Flags().SortFlags = false

	restartXClusterCmd.Flags().StringP("uuid", "u", "",
		"[Required] The uuid of the xcluster to restart.")
	restartXClusterCmd.MarkFlagRequired("uuid")

	restartXClusterCmd.Flags().String("storage-config-name", "",
		"[Required] Storage config to be used for taking the backup for replication. ")
	restartXClusterCmd.MarkFlagRequired("storage-config-name")

	restartXClusterCmd.Flags().Bool("dry-run", false,
		"[Optional] Run the pre-checks without actually running the subtasks. (default false)")

	restartXClusterCmd.Flags().String("table-uuids", "",
		"[Optional] Comma separated list of source universe table IDs/UUIDs to restart. "+
			"If not specified, all tables will be restarted.")

	restartXClusterCmd.Flags().Int("parallelism", 8,
		"[Optional] Number of concurrent commands to run on nodes over SSH via \"yb_backup\" script.")

	restartXClusterCmd.Flags().Bool("force-delete", false,
		"[Optional] Force delete components of the universe xcluster despite errors during restart. "+
			"May leave stale states and replication streams on the participating universes. "+
			"(default false)")

	restartXClusterCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
