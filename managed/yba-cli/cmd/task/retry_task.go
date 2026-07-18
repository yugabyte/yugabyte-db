/*
 * Copyright (c) YugabyteDB, Inc.
 */

package task

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
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/task"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/ybatask"
)

// retryTaskCmd represents the retry task command
var retryTaskCmd = &cobra.Command{
	Use:     "retry",
	Short:   "Retry a YugabyteDB Anywhere task",
	Long:    "Retry a task in YugabyteDB Anywhere",
	Example: `yba task retry --uuid <uuid>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		taskUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(taskUUID) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No task UUID found to retry\n", formatter.RedColor))
		}

	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		taskUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		retryRequest := authAPI.RetryTask(taskUUID)

		rTask, response, err := retryRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Task", "Retry")
		}

		util.CheckTaskAfterCreation(rTask)

		tasks, response, err := authAPI.TasksList().Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Task", "Retry - Fetch Tasks")
		}

		newTaskUUID := rTask.GetTaskUUID()
		var currentTask ybaclient.CustomerTaskData
		for _, t := range tasks {
			if strings.Compare(t.GetId(), newTaskUUID) == 0 {
				currentTask = t
			}
		}

		msg := fmt.Sprintf("The task - \"(%s)\" (%s) is being retried",
			formatter.Colorize(currentTask.GetTitle(), formatter.GreenColor), newTaskUUID)

		if viper.GetBool("wait") {
			if rTask.GetTaskUUID() != "" {
				logrus.Info(fmt.Sprintf("Waiting for task %s (%s) task to be retried\n",
					formatter.Colorize(currentTask.GetTitle(), formatter.GreenColor), newTaskUUID))
				err = authAPI.WaitForTask(newTaskUUID, msg)
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
			}
			logrus.Infof("The task - \"(%s)\" (%s) has been completed\n",
				formatter.Colorize(currentTask.GetTitle(), formatter.GreenColor),
				newTaskUUID)
			tasksCtx := formatter.Context{
				Command: "retry",
				Output:  os.Stdout,
				Format:  task.NewTaskFormat(viper.GetString("output")),
			}

			tasks, response, err := authAPI.TasksList().Execute()
			if err != nil {
				util.FatalHTTPError(response, err, "Task", "Retry - Fetch Tasks")
			}

			newTaskUUID := rTask.GetTaskUUID()
			var currentTask ybaclient.CustomerTaskData
			for _, t := range tasks {
				if strings.Compare(t.GetId(), newTaskUUID) == 0 {
					currentTask = t
				}
			}
			task.Write(tasksCtx, []ybaclient.CustomerTaskData{currentTask})
			return
		}
		logrus.Infoln(msg + "\n")
		taskCtx := formatter.Context{
			Command: "retry",
			Output:  os.Stdout,
			Format:  ybatask.NewTaskFormat(viper.GetString("output")),
		}
		ybatask.Write(taskCtx, []ybaclient.YBPTask{*rTask})
	},
}

func init() {
	retryTaskCmd.Flags().SortFlags = false
	retryTaskCmd.Flags().StringP("uuid", "u", "",
		"[Required] The task UUID to be retried.")
	retryTaskCmd.MarkFlagRequired("uuid")

}
