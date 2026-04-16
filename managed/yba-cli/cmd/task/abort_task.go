/*
 * Copyright (c) YugabyteDB, Inc.
 */

package task

import (
	"fmt"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// abortTaskCmd represents the abort task command
var abortTaskCmd = &cobra.Command{
	Use:     "abort",
	Short:   "Abort a YugabyteDB Anywhere task",
	Long:    "Abort a task in YugabyteDB Anywhere",
	Example: `yba task abort --uuid <task-uuid>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		taskUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(taskUUID) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No task UUID found to abort\n", formatter.RedColor))
		}

	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		taskUUID, err := cmd.Flags().GetString("uuid")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		abortRequest := authAPI.AbortTask(taskUUID)

		rTask, response, err := abortRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Task", "Abort")
		}

		if rTask.GetSuccess() {
			logrus.Info(fmt.Sprintf("The task %s has been aborted\n",
				formatter.Colorize(taskUUID, formatter.GreenColor)))

		} else {
			logrus.Errorf(
				formatter.Colorize(
					fmt.Sprintf(
						"An error occurred while aborting task %s\n",
						formatter.Colorize(taskUUID, formatter.GreenColor)),
					formatter.RedColor))
		}
	},
}

func init() {
	abortTaskCmd.Flags().SortFlags = false
	abortTaskCmd.Flags().StringP("uuid", "u", "",
		"[Required] The task UUID to be aborted.")
	abortTaskCmd.MarkFlagRequired("uuid")

}
