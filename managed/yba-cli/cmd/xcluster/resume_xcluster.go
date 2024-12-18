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

var resumeXClusterCmd = &cobra.Command{
	Use:     "resume",
	Short:   "Resume a YugabyteDB Anywhere xCluster",
	Long:    "Resume a xCluster in YugabyteDB Anywhere between two universes",
	Example: `yba xcluster resume --uuid <xcluster-uuid>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		uuid, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(uuid) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No xcluster uuid found to resume\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		uuid, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		req := ybaclient.XClusterConfigEditFormData{
			Status: util.GetStringPointer("Running"),
		}

		rTask, response, err := authAPI.EditXClusterConfig(uuid).
			XclusterReplicationEditFormData(req).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "XCluster", "Resume")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		msg := fmt.Sprintf("The xcluster %s is being resumed",
			formatter.Colorize(uuid, formatter.GreenColor))

		if viper.GetBool("wait") {
			if len(rTask.GetTaskUUID()) > 0 {
				logrus.Info(fmt.Sprintf("Waiting for xcluster %s to be resumed\n",
					formatter.Colorize(uuid, formatter.GreenColor)))
				err = authAPI.WaitForTask(rTask.GetTaskUUID(), msg)
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
			}
			logrus.Infof("The xcluster %s has been resumeed\n",
				formatter.Colorize(uuid, formatter.GreenColor))
			return
		}
		logrus.Infoln(msg + "\n")
		taskCtx := formatter.Context{
			Command: "resume",
			Output:  os.Stdout,
			Format:  ybatask.NewTaskFormat(viper.GetString("output")),
		}
		ybatask.Write(taskCtx, []ybaclient.YBPTask{rTask})

	},
}

func init() {
	resumeXClusterCmd.Flags().SortFlags = false

	resumeXClusterCmd.Flags().StringP("uuid", "u", "",
		"[Required] The uuid of the xcluster to resume.")
	resumeXClusterCmd.MarkFlagRequired("uuid")
}
