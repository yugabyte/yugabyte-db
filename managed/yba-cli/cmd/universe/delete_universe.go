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

// deleteUniverseCmd represents the universe command
var deleteUniverseCmd = &cobra.Command{
	Use:     "delete",
	Aliases: []string{"remove", "rm"},
	GroupID: "action",
	Short:   "Delete a YugabyteDB Anywhere universe",
	Long:    "Delete a universe in YugabyteDB Anywhere",
	Example: `yba universe delete --name <universe-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(universeName) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to delete\n", formatter.RedColor))
		}
		err = util.ConfirmCommand(
			fmt.Sprintf("Are you sure you want to delete %s: %s", util.UniverseType, universeName),
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
			util.FatalHTTPError(response, err, "Universe", "Delete - List Universes")
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

		deleteUniverseRequest := authAPI.DeleteUniverse(universeUUID)

		forceDelete, err := cmd.Flags().GetBool("force-delete")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		deleteBackups, err := cmd.Flags().GetBool("delete-backups")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		deleteCerts, err := cmd.Flags().GetBool("delete-certs")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		deleteUniverseRequest = deleteUniverseRequest.IsForceDelete(forceDelete).
			IsDeleteBackups(deleteBackups).IsDeleteAssociatedCerts(deleteCerts)

		rTask, response, err := deleteUniverseRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Universe", "Delete")
		}

		util.CheckTaskAfterCreation(rTask)

		msg := fmt.Sprintf("The universe %s (%s) is being deleted",
			formatter.Colorize(universeName, formatter.GreenColor), universeUUID)

		if viper.GetBool("wait") {
			if len(rTask.GetTaskUUID()) > 0 {
				logrus.Info(fmt.Sprintf("Waiting for universe %s (%s) to be deleted\n",
					formatter.Colorize(universeName, formatter.GreenColor), universeUUID))
				err = authAPI.WaitForTask(rTask.GetTaskUUID(), msg)
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
			}
			logrus.Infof("The universe %s (%s) has been deleted\n",
				formatter.Colorize(universeName, formatter.GreenColor), universeUUID)
			return
		}
		logrus.Infoln(msg + "\n")
		taskCtx := formatter.Context{
			Command: "delete",
			Output:  os.Stdout,
			Format:  ybatask.NewTaskFormat(viper.GetString("output")),
		}
		ybatask.Write(taskCtx, []ybaclient.YBPTask{*rTask})
	},
}

func init() {
	deleteUniverseCmd.Flags().SortFlags = false
	deleteUniverseCmd.Flags().StringP("name", "n", "",
		"[Required] The name of the universe to be deleted.")
	deleteUniverseCmd.MarkFlagRequired("name")
	deleteUniverseCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
	deleteUniverseCmd.Flags().Bool("force-delete", false,
		"[Optional] Force delete the universe despite errors. (default false)")
	deleteUniverseCmd.Flags().Bool("delete-backups", false,
		"[Optional] Delete backups associated with name. (default false)")
	deleteUniverseCmd.Flags().Bool("delete-certs", false,
		"[Optional] Delete certs associated with name. (default false)")
}
