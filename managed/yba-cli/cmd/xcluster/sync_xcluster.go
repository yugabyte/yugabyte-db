/*
 * Copyright (c) YugabyteDB, Inc.
 */

package xcluster

import (
	"fmt"
	"os"

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

var syncXClusterCmd = &cobra.Command{
	Use:   "sync",
	Short: "Reconcile a YugabyteDB Anywhere xcluster configuration with database",
	Long: "If changes have been made to your xCluster configuration outside of " +
		"YugabyteDB Anywhere (for example, using yb-admin), reconciling the xCluster " +
		"configuration with the database updates the configuration in " +
		"YugabyteDB Anywhere to match the changes.",
	Example: `yba xcluster sync --uuid <xcluster-uuid>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		uuid, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(uuid) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No xcluster uuid found to sync\n", formatter.RedColor))
		}
		err = util.ConfirmCommand(
			fmt.Sprintf("Are you sure you want to sync %s: %s", "xcluster", uuid),
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

		rTask, response, err := authAPI.SyncXClusterConfig(uuid).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "xCluster", "Sync")
		}

		util.CheckTaskAfterCreation(rTask)

		msg := fmt.Sprintf("The xcluster %s is being synced",
			formatter.Colorize(uuid, formatter.GreenColor))

		if viper.GetBool("wait") {
			if len(rTask.GetTaskUUID()) > 0 {
				logrus.Info(fmt.Sprintf("Waiting for xcluster %s to be synced\n",
					formatter.Colorize(uuid, formatter.GreenColor)))
				err = authAPI.WaitForTask(rTask.GetTaskUUID(), msg)
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
			}
			logrus.Infof("The xcluster %s has been synced\n",
				formatter.Colorize(uuid, formatter.GreenColor))
			rXCluster, response, err := authAPI.GetXClusterConfig(uuid).Execute()
			if err != nil {
				util.FatalHTTPError(response, err, "xCluster", "Sync - Get xCluster")
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
				"Sync",
			)

			xcluster.SourceUniverse = sourceUniverse
			xcluster.TargetUniverse = targetUniverse

			xclusterCtx := formatter.Context{
				Command: "sync",
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
			Command: "sync",
			Output:  os.Stdout,
			Format:  ybatask.NewTaskFormat(viper.GetString("output")),
		}
		ybatask.Write(taskCtx, []ybaclient.YBPTask{task})

	},
}

func init() {
	syncXClusterCmd.Flags().SortFlags = false

	syncXClusterCmd.Flags().StringP("uuid", "u", "",
		"[Required] The uuid of the xcluster to sync.")
	syncXClusterCmd.MarkFlagRequired("uuid")

	syncXClusterCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
}
