/*
 * Copyright (c) YugabyteDB, Inc.
 */

package universe

import (
	"fmt"
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/ybatask"
)

// resumeUniverseCmd represents the universe command
var resumeUniverseCmd = &cobra.Command{
	Use:     "resume",
	GroupID: "action",
	Short:   "Resume a YugabyteDB Anywhere universe",
	Long:    "Resume a universe in YugabyteDB Anywhere",
	Example: `yba universe resume --name <universe-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(universeName) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to resume\n", formatter.RedColor))
		}
		err = util.ConfirmCommand(
			fmt.Sprintf("Are you sure you want to resume %s: %s", util.UniverseType, universeName),
			viper.GetBool("force"))
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		universeListRequest := authAPI.ListUniverses()
		universeListRequest = universeListRequest.Name(universeName)

		r, response, err := universeListRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Universe", "Resume - List Universes")
		}

		if len(r) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No universes with name: %s found\n", universeName),
					formatter.RedColor,
				))
		}

		var universeUUID string
		if len(r) > 0 {
			universeUUID = r[0].GetUniverseUUID()
		}

		rTask, response, err := authAPI.ResumeUniverse(universeUUID).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Universe", "Resume")
		}

		util.CheckTaskAfterCreation(rTask)

		msg := fmt.Sprintf("The universe %s (%s) is being resumed",
			formatter.Colorize(universeName, formatter.GreenColor), universeUUID)

		if viper.GetBool("wait") {
			if len(rTask.GetTaskUUID()) > 0 {
				logrus.Info(fmt.Sprintf("Waiting for universe %s (%s) to be resumed\n",
					formatter.Colorize(universeName, formatter.GreenColor), universeUUID))
				err = authAPI.WaitForTask(rTask.GetTaskUUID(), msg)
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
			}
			logrus.Infof("The universe %s (%s) has been resumed\n",
				formatter.Colorize(universeName, formatter.GreenColor), universeUUID)
			return
		}
		logrus.Infoln(msg + "\n")
		taskCtx := formatter.Context{
			Command: "resume",
			Output:  os.Stdout,
			Format:  ybatask.NewTaskFormat(viper.GetString("output")),
		}
		ybatask.Write(taskCtx, []ybaclient.YBPTask{*rTask})
	},
}

func init() {
	resumeUniverseCmd.Flags().SortFlags = false
	resumeUniverseCmd.Flags().StringP("name", "n", "",
		"[Required] The name of the universe to be resumed.")
	resumeUniverseCmd.MarkFlagRequired("name")
	resumeUniverseCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")

}
