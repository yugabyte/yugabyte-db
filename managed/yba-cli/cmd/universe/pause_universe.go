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
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/universeutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/ybatask"
)

// pauseUniverseCmd represents the universe command
var pauseUniverseCmd = &cobra.Command{
	Use:     "pause",
	GroupID: "action",
	Short:   "Pause a YugabyteDB Anywhere universe",
	Long:    "Pause a universe. Fails if it is already paused or has a task in progress.",
	Example: `yba universe pause --name <universe-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(universeName) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to pause\n", formatter.RedColor))
		}
		_, universe, err := universeutil.Validations(cmd, "Pause")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
		}
		err = checkPauseResumeState(universe, true)
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		err = util.ConfirmCommand(
			fmt.Sprintf("Are you sure you want to pause %s: %s", util.UniverseType, universeName),
			viper.GetBool("force"))
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI, universe, err := universeutil.Validations(cmd, "Pause")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
		}
		universeName := universe.GetName()
		universeUUID := universe.GetUniverseUUID()

		rTask, response, err := authAPI.PauseUniverse(universeUUID).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Universe", "Pause")
		}

		util.CheckTaskAfterCreation(rTask)

		msg := fmt.Sprintf("The universe %s (%s) is being paused",
			formatter.Colorize(universeName, formatter.GreenColor), universeUUID)

		if viper.GetBool("wait") {
			if len(rTask.GetTaskUUID()) > 0 {
				logrus.Info(fmt.Sprintf("Waiting for universe %s (%s) to be paused\n",
					formatter.Colorize(universeName, formatter.GreenColor), universeUUID))
				err = authAPI.WaitForTask(rTask.GetTaskUUID(), msg)
				if err != nil {
					logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
			}
			logrus.Infof("The universe %s (%s) has been paused\n",
				formatter.Colorize(universeName, formatter.GreenColor), universeUUID)
			return
		}
		logrus.Infoln(msg + "\n")
		taskCtx := formatter.Context{
			Command: "pause",
			Output:  os.Stdout,
			Format:  ybatask.NewTaskFormat(viper.GetString("output")),
		}
		ybatask.Write(taskCtx, []ybaclient.YBPTask{*rTask})
	},
}

// checkPauseResumeState rejects a pause or resume that YBA would fail or that would do nothing.
func checkPauseResumeState(universe ybaclient.UniverseResp, pause bool) error {
	details := universe.GetUniverseDetails()
	name := universe.GetName()
	if details.GetUpdateInProgress() {
		return fmt.Errorf("Universe %s has a task in progress. Wait for it to finish", name)
	}
	paused := details.GetUniversePaused()
	if pause && paused {
		return fmt.Errorf("Universe %s is already paused", name)
	}
	if !pause && !paused {
		return fmt.Errorf("Universe %s is not paused", name)
	}
	return nil
}

func init() {
	pauseUniverseCmd.Flags().SortFlags = false
	pauseUniverseCmd.Flags().StringP("name", "n", "",
		"[Required] The name of the universe to be paused.")
	pauseUniverseCmd.MarkFlagRequired("name")
	pauseUniverseCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")

}
