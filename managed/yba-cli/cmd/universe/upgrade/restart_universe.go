/*
 * Copyright (c) YugaByte, Inc.
 */

package upgrade

import (
	"fmt"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// RestartCmd represents the universe upgrade restart command
var RestartCmd = &cobra.Command{
	Use:   "restart",
	Short: "Restart a YugabyteDB Anywhere Universe",
	Long:  "Restart a YugabyteDB Anywhere Universe",
	PreRun: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(universeName) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to restart\n", formatter.RedColor))
		}

		// Validations before restart operation
		skipValidations, err := cmd.Flags().GetBool("skip-validations")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !skipValidations {
			_, _, err := Validations(cmd, util.UpgradeOperation)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}
		err = util.ConfirmCommand(
			fmt.Sprintf("Are you sure you want to restart %s: %s",
				util.UniverseType, universeName),
			viper.GetBool("force"))
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {

		authAPI, universe, err := Validations(cmd, util.UpgradeOperation)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		universeName := universe.GetName()
		universeUUID := universe.GetUniverseUUID()
		universeDetails := universe.GetUniverseDetails()
		clusters := universeDetails.GetClusters()

		upgradeOption, err := cmd.Flags().GetString("upgrade-option")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		masterDelay, err := cmd.Flags().GetInt32("delay-between-master-servers")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		tserverDelay, err := cmd.Flags().GetInt32("delay-between-tservers")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		req := ybaclient.RestartTaskParams{
			Clusters:                       clusters,
			UpgradeOption:                  upgradeOption,
			SleepAfterTServerRestartMillis: tserverDelay,
			SleepAfterMasterRestartMillis:  masterDelay,
		}

		rUpgrade, response, err := authAPI.RestartUniverse(universeUUID).
			RestartTaskParams(req).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Universe", "Restart")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		taskUUID := rUpgrade.GetTaskUUID()
		logrus.Info(
			fmt.Sprintf("Restarting universe %s\n",
				formatter.Colorize(universeName, formatter.GreenColor)))

		WaitForUpgradeUniverseTask(authAPI, universeName, universeUUID, taskUUID)
	},
}

func init() {
	RestartCmd.Flags().SortFlags = false

	RestartCmd.Flags().StringP("name", "n", "",
		"[Required] The name of the universe to be restarted.")
	RestartCmd.MarkFlagRequired("name")

	RestartCmd.Flags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
	RestartCmd.Flags().BoolP("skip-validations", "s", false,
		"[Optional] Skip validations before running the CLI command.")

	RestartCmd.Flags().Int32("delay-between-master-servers",
		18000, "[Optional] Upgrade delay between Master servers (in miliseconds).")
	RestartCmd.Flags().Int32("delay-between-tservers",
		18000, "[Optional] Upgrade delay between Tservers (in miliseconds).")

	RestartCmd.Flags().String("upgrade-option", "Rolling",
		"[Optional] Upgrade Options, defaults to Rolling. "+
			"Allowed values (case sensitive): Rolling, Non-Rolling (involves DB downtime). "+
			"Only a \"Rolling\" type of restart is allowed on a Kubernetes universe.")
}
