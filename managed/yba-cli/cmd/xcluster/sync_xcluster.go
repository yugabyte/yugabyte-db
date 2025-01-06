/*
 * Copyright (c) YugaByte, Inc.
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
		uuid, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(uuid) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No xcluster uuid found to sync\n", formatter.RedColor))
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
			errMessage := util.ErrorFromHTTPResponse(response, err, "XCluster", "Sync")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

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
			return
		}
		logrus.Infoln(msg + "\n")
		taskCtx := formatter.Context{
			Command: "sync",
			Output:  os.Stdout,
			Format:  ybatask.NewTaskFormat(viper.GetString("output")),
		}
		ybatask.Write(taskCtx, []ybaclient.YBPTask{rTask})

	},
}

func init() {
	syncXClusterCmd.Flags().SortFlags = false

	syncXClusterCmd.Flags().StringP("uuid", "u", "",
		"[Required] The uuid of the xcluster to sync.")
	syncXClusterCmd.MarkFlagRequired("uuid")
}
