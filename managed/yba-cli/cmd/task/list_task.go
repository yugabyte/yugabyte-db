/*
 * Copyright (c) YugaByte, Inc.
 */

package task

import (
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/task"
)

var listTaskCmd = &cobra.Command{
	Use:   "list",
	Short: "List YugabyteDB Anywhere tasks",
	Long:  "List YugabyteDB Anywhere tasks",
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		r, response, err := authAPI.TasksList().Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Task", "List")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}
		// filter by uuid
		taskUUID, err := cmd.Flags().GetString("task-uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(taskUUID) > 0 {
			var currentTask ybaclient.CustomerTaskData
			for _, t := range r {
				if strings.Compare(t.GetId(), taskUUID) == 0 {
					currentTask = t
				}
			}
			r = []ybaclient.CustomerTaskData{
				currentTask,
			}
		}

		taskCtx := formatter.Context{
			Output: os.Stdout,
			Format: task.NewTaskFormat(viper.GetString("output")),
		}
		if len(r) < 1 {
			if util.IsOutputType("table") {
				logrus.Infoln("No tasks found\n")
			} else {
				logrus.Infoln("{}\n")
			}
			return
		}
		task.Write(taskCtx, r)

	},
}

func init() {
	listTaskCmd.Flags().SortFlags = false

	listTaskCmd.Flags().String("task-uuid", "", "[Optional] UUID of the task.")

}
