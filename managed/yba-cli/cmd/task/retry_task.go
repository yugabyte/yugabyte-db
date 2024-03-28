/*
 * Copyright (c) YugaByte, Inc.
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
)

// retryTaskCmd represents the retry task command
var retryTaskCmd = &cobra.Command{
	Use:   "retry",
	Short: "Retry a YugabyteDB Anywhere task",
	Long:  "Retry a task in YugabyteDB Anywhere",
	PreRun: func(cmd *cobra.Command, args []string) {
		taskUUID, err := cmd.Flags().GetString("task-uuid")
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

		taskUUID, err := cmd.Flags().GetString("task-uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		retryRequest := authAPI.RetryTask(taskUUID)

		rRetry, response, err := retryRequest.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Task", "Retry")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		tasks, response, err := authAPI.TasksList().Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Task", "Retry - Fetch Tasks")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		newTaskUUID := rRetry.GetTaskUUID()
		var currentTask ybaclient.CustomerTaskData
		for _, t := range tasks {
			if strings.Compare(t.GetId(), newTaskUUID) == 0 {
				currentTask = t
			}
		}

		msg := fmt.Sprintf("The task - \"(%s)\" (%s)  is being retried",
			formatter.Colorize(currentTask.GetTitle(), formatter.GreenColor), newTaskUUID)

		if viper.GetBool("wait") {
			if rRetry.GetTaskUUID() != "" {
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
				Output: os.Stdout,
				Format: task.NewTaskFormat(viper.GetString("output")),
			}

			tasks, response, err := authAPI.TasksList().Execute()
			if err != nil {
				errMessage := util.ErrorFromHTTPResponse(response, err, "Task", "Retry - Fetch Tasks")
				logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
			}

			newTaskUUID := rRetry.GetTaskUUID()
			var currentTask ybaclient.CustomerTaskData
			for _, t := range tasks {
				if strings.Compare(t.GetId(), newTaskUUID) == 0 {
					currentTask = t
				}
			}
			task.Write(tasksCtx, []ybaclient.CustomerTaskData{currentTask})

		}
		logrus.Infoln(msg + "\n")
	},
}

func init() {
	retryTaskCmd.Flags().SortFlags = false
	retryTaskCmd.Flags().String("task-uuid", "",
		"[Required] The task UUID to be retried.")
	retryTaskCmd.MarkFlagRequired("task-uuid")

}
