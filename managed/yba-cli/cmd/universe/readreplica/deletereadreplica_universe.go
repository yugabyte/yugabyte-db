/*
 * Copyright (c) YugabyteDB, Inc.
 */

package readreplica

import (
	"fmt"
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/universeutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/universe"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/ybatask"
)

// DeleteReadReplicaUniverseCmd represents the universe command
var DeleteReadReplicaUniverseCmd = &cobra.Command{
	Use:     "delete-read-replica",
	Aliases: []string{"remove-read-replica", "rm-read-replica"},
	GroupID: "read-replica",
	Short:   "Delete a YugabyteDB Anywhere universe Read Replica",
	Long:    "Delete Read replica from a universe in YugabyteDB Anywhere",
	Example: `yba universe delete-read-replica -n <universe-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(universeName) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to delete read replica from\n",
					formatter.RedColor))
		}
		skipValidations, err := cmd.Flags().GetBool("skip-validations")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !skipValidations {
			_, _, err := universeutil.Validations(cmd, util.EditOperation)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}
		err = util.ConfirmCommand(
			fmt.Sprintf("Are you sure you want to delete read replica from %s: %s",
				"universe", universeName),
			viper.GetBool("force"))
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {

		authAPI, universeReq, err := universeutil.Validations(cmd, "Delete Read Only Cluster")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		universeName := universeReq.GetName()
		universeUUID := universeReq.GetUniverseUUID()
		details := universeReq.GetUniverseDetails()
		clusters := details.GetClusters()
		if len(clusters) < 1 {
			err := fmt.Errorf(
				"No clusters found in universe " + universeName + " (" + universeUUID + ")")
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if len(clusters) < 2 {
			err := fmt.Errorf(
				"No Read replica cluster found in universe " +
					universeName + " (" + universeUUID + ") to delete")
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		var cluster ybaclient.Cluster
		for _, c := range clusters {
			if strings.EqualFold(c.GetClusterType(), util.ReadReplicaClusterType) {
				cluster = c
				break
			}
		}

		if len(cluster.GetUuid()) == 0 {
			err := fmt.Errorf(
				"No Read replica cluster found in universe " +
					universeName + " (" + universeUUID + ") to delete")
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		forceDelete, err := cmd.Flags().GetBool("force-delete")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		deleteRR := authAPI.
			DeleteReadonlyCluster(universeUUID, cluster.GetUuid()).
			IsForceDelete(forceDelete)

		rTask, response, err := deleteRR.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Universe", "Delete Read Only Cluster")
		}

		util.CheckTaskAfterCreation(rTask)

		msg := fmt.Sprintf("The read replica from universe %s is being deleted",
			formatter.Colorize(universeName, formatter.GreenColor))

		if viper.GetBool("wait") {
			if rTask.TaskUUID != nil {
				logrus.Info(fmt.Sprintf("Waiting for read replica from "+
					"universe %s (%s) to be deleted\n",
					formatter.Colorize(universeName, formatter.GreenColor), universeUUID))
				err = authAPI.WaitForTask(rTask.GetTaskUUID(), msg)
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
			}
			logrus.Infof("The read replica from universe %s (%s) has been deleted\n",
				formatter.Colorize(universeName, formatter.GreenColor), universeUUID)
			universeData, response, err := authAPI.ListUniverses().Name(universeName).Execute()
			if err != nil {
				util.FatalHTTPError(response, err, "Universe", "Create - Fetch Universe")
			}

			universesCtx := formatter.Context{
				Command: "delete",
				Output:  os.Stdout,
				Format:  universe.NewUniverseFormat(viper.GetString("output")),
			}

			universe.Write(universesCtx, universeData)
			return
		}
		logrus.Infoln(msg + "\n")
		taskCtx := formatter.Context{
			Command: "create",
			Output:  os.Stdout,
			Format:  ybatask.NewTaskFormat(viper.GetString("output")),
		}
		ybatask.Write(taskCtx, []ybaclient.YBPTask{*rTask})
	},
}

func init() {
	DeleteReadReplicaUniverseCmd.Flags().SortFlags = false
	DeleteReadReplicaUniverseCmd.Flags().StringP("name", "n", "",
		"[Required] The name of the universe.")
	DeleteReadReplicaUniverseCmd.MarkFlagRequired("name")
	DeleteReadReplicaUniverseCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
	DeleteReadReplicaUniverseCmd.Flags().Bool("force-delete", false,
		"[Optional] Force delete the universe read replica despite errors, defaults to false.")
	DeleteReadReplicaUniverseCmd.Flags().BoolP("skip-validations", "s", false,
		"[Optional] Skip validations before running the CLI command.")

}
