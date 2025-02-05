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

var pauseXClusterCmd = &cobra.Command{
	Use:     "pause",
	Short:   "Pause a YugabyteDB Anywhere xCluster",
	Long:    "Pause a xCluster in YugabyteDB Anywhere between two universes",
	Example: `yba xcluster pause --uuid <xcluster-uuid>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		uuid, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(uuid) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No xcluster uuid found to pause\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		uuid, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		req := ybaclient.XClusterConfigEditFormData{
			Status: util.GetStringPointer("Paused"),
		}

		rTask, response, err := authAPI.EditXClusterConfig(uuid).
			XclusterReplicationEditFormData(req).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "XCluster", "Pause")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		msg := fmt.Sprintf("The xcluster %s is being pauseed",
			formatter.Colorize(uuid, formatter.GreenColor))

		if viper.GetBool("wait") {
			if len(rTask.GetTaskUUID()) > 0 {
				logrus.Info(fmt.Sprintf("Waiting for xcluster %s to be pauseed\n",
					formatter.Colorize(uuid, formatter.GreenColor)))
				err = authAPI.WaitForTask(rTask.GetTaskUUID(), msg)
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
			}
			logrus.Infof("The xcluster %s has been pauseed\n",
				formatter.Colorize(uuid, formatter.GreenColor))
			return
		}
		logrus.Infoln(msg + "\n")
		taskCtx := formatter.Context{
			Command: "pause",
			Output:  os.Stdout,
			Format:  ybatask.NewTaskFormat(viper.GetString("output")),
		}
		ybatask.Write(taskCtx, []ybaclient.YBPTask{rTask})

	},
}

func init() {
	pauseXClusterCmd.Flags().SortFlags = false

	pauseXClusterCmd.Flags().StringP("uuid", "u", "",
		"[Required] The uuid of the xcluster to pause.")
	pauseXClusterCmd.MarkFlagRequired("uuid")
}
