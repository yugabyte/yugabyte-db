/*
 * Copyright (c) YugabyteDB, Inc.
 */

package upgrade

import (
	"fmt"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/universeutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// upgradeRollbackCmd represents the universe upgrade rollback command
var upgradeRollbackCmd = &cobra.Command{
	Use:   "rollback",
	Short: "Roll back the software upgrade of a YugabyteDB Anywhere Universe",
	Long: "Roll back the software upgrade of a YugabyteDB Anywhere Universe " +
		"to the previous YugabyteDB version. Run it before the upgrade is finalized." +
		upgradeFlowHelp,
	Example: `yba universe upgrade rollback --name <universe-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		universeName := util.MustGetFlagString(cmd, "name")

		skipValidations := util.MustGetFlagBool(cmd, "skip-validations")
		upgradeOption := util.MustGetFlagString(cmd, "upgrade-option")
		if upgradeOption != "Rolling" && upgradeOption != "Non-Rolling" {
			logrus.Fatal(formatter.Colorize(
				"Invalid --upgrade-option "+upgradeOption+
					", allowed values (case sensitive): Rolling, Non-Rolling\n",
				formatter.RedColor))
		}
		prompt := fmt.Sprintf("Are you sure you want to roll back the software upgrade of %s: %s",
			util.UniverseType, universeName)
		if !skipValidations {
			_, universe, err := universeutil.Validations(cmd, util.UpgradeOperation)
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			details := universe.GetUniverseDetails()
			if !details.GetIsSoftwareRollbackAllowed() {
				logrus.Fatal(formatter.Colorize(fmt.Sprintf(
					"Cannot roll back: the last software upgrade of universe %s was finalized\n",
					universeName), formatter.RedColor))
			}
			current, previous := rollbackVersions(universe)
			if previous != "" {
				prompt = fmt.Sprintf("%s from version %s to version %s", prompt, current, previous)
			}
		}
		err := util.ConfirmCommand(prompt, viper.GetBool("force"))
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI, universe, err := universeutil.Validations(cmd, util.UpgradeOperation)
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		universeName := universe.GetName()
		universeUUID := universe.GetUniverseUUID()

		upgradeOption := util.MustGetFlagString(cmd, "upgrade-option")
		masterDelay, err := cmd.Flags().GetInt32("delay-between-master-servers")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		tserverDelay, err := cmd.Flags().GetInt32("delay-between-tservers")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		batchSize := universeutil.RollMaxBatchSizeFromFlags(cmd, upgradeOption)
		req := ybaclient.RollbackUpgradeParams{
			Clusters:                       universe.GetUniverseDetails().Clusters,
			UpgradeOption:                  upgradeOption,
			SleepAfterMasterRestartMillis:  masterDelay,
			SleepAfterTServerRestartMillis: tserverDelay,
			RollMaxBatchSize:               batchSize,
		}

		rTask, response, err := authAPI.RollbackUpgrade(universeUUID).
			RollbackUpgradeParams(req).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Universe", "Rollback Upgrade")
		}

		msg := fmt.Sprintf("Rolling back universe %s (%s)",
			formatter.Colorize(universeName, formatter.GreenColor), universeUUID)
		if current, previous := rollbackVersions(universe); previous != "" {
			msg = fmt.Sprintf("%s from version %s to %s", msg, current, previous)
		}
		logrus.Info(msg + "\n")

		universeutil.WaitForUpgradeUniverseTask(authAPI, universeName, rTask)
	},
}

// rollbackVersions returns the current version and the version a rollback goes back to.
func rollbackVersions(universe ybaclient.UniverseResp) (string, string) {
	details := universe.GetUniverseDetails()
	var current string
	if clusters := details.GetClusters(); len(clusters) > 0 {
		primary, _ := universeutil.MustFindPrimaryCluster(
			clusters, universe.GetName(), universe.GetUniverseUUID())
		userIntent := primary.GetUserIntent()
		current = userIntent.GetYbSoftwareVersion()
	}
	prev := details.GetPrevYBSoftwareConfig()
	return current, prev.GetSoftwareVersion()
}

func init() {
	upgradeRollbackCmd.Flags().SortFlags = false

	upgradeRollbackCmd.Flags().String("upgrade-option", "Rolling",
		"[Optional] Rollback options, defaults to Rolling. "+
			"Allowed values (case sensitive): Rolling, Non-Rolling (involves DB downtime)")
	upgradeRollbackCmd.Flags().Int32("delay-between-master-servers",
		18000, "[Optional] Delay between Master server restarts (in milliseconds).")
	upgradeRollbackCmd.Flags().Int32("delay-between-tservers",
		18000, "[Optional] Delay between Tserver restarts (in milliseconds).")
	universeutil.AddRollMaxBatchSizeFlags(upgradeRollbackCmd)
}
