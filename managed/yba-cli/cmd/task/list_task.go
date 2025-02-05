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
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "List YugabyteDB Anywhere tasks",
	Long:    "List YugabyteDB Anywhere tasks",
	Example: `yba task list`,
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		universeName, err := cmd.Flags().GetString("universe-name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		universeUUID := ""
		isUniverseUUIDPresent := false

		if len(strings.TrimSpace(universeName)) != 0 {
			r, response, err := authAPI.ListUniverses().Name(universeName).Execute()
			if err != nil {
				errMessage := util.ErrorFromHTTPResponse(response, err, "Task", "List - List Universes")
				logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
			}
			if len(r) < 1 {
				if util.IsOutputType(formatter.TableFormatKey) {
					logrus.Info("No universes with name " + universeName + " found\n")
				} else {
					logrus.Info("[]\n")
				}
				return
			}
			universeUUID = r[0].GetUniverseUUID()
		}

		listTasksRequest := authAPI.TasksList()

		if len(strings.TrimSpace(universeUUID)) != 0 {
			isUniverseUUIDPresent = true
			listTasksRequest = listTasksRequest.UUUID(universeUUID)
		}

		r, response, err := listTasksRequest.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Task", "List")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}
		// filter by uuid
		taskUUID, err := cmd.Flags().GetString("uuid")
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
			Command: "list",
			Output:  os.Stdout,
			Format:  task.NewTaskFormat(viper.GetString("output")),
		}
		if len(r) < 1 {
			if util.IsOutputType(formatter.TableFormatKey) {
				if isUniverseUUIDPresent {
					logrus.Info("No tasks found in universe " + universeName + "\n")
				} else {
					logrus.Info("No tasks found\n")
				}
			} else {
				logrus.Info("[]\n")
			}
			return
		}
		task.Write(taskCtx, r)

	},
}

func init() {
	listTaskCmd.Flags().SortFlags = false

	listTaskCmd.Flags().StringP("uuid", "u", "", "[Optional] UUID of the task.")
	listTaskCmd.Flags().String("universe-name", "",
		"[Optional] The name of the universe whose tasks are to be listed.")

}
