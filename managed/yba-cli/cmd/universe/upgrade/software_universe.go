/*
 * Copyright (c) YugabyteDB, Inc.
 */

package upgrade

import (
	"fmt"
	"net/http"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/pflag"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/universeutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// Software upgrade states that the CLI checks.
const (
	preFinalizeState    = "PreFinalize"
	finalizeFailedState = "FinalizeFailed"
)

// upgradeFlowHelp describes the software upgrade, finalize and rollback flow.
const upgradeFlowHelp = `

Software upgrade flow:
  1. "yba universe upgrade software" upgrades the nodes. If the target release needs a
     finalize, the universe stays in PreFinalize state and the upgrade can be rolled back.
  2. "yba universe describe" shows the Software Upgrade State and Rollback Allowed.
  3. Do one of these:
     - "yba universe upgrade finalize" completes the upgrade. After it, rollback is not
       possible.
     - "yba universe upgrade rollback" returns the universe to the previous version.`

// upgradeSoftwareCmd represents the universe upgrade software command
var upgradeSoftwareCmd = &cobra.Command{
	Use:     "software",
	Aliases: []string{"yb-db-version"},
	Short:   "Software upgrade for a YugabyteDB Anywhere Universe",
	Long:    "Software upgrade for a YugabyteDB Anywhere Universe.",
	Example: `yba universe upgrade software --name <universe-name> \
	--yb-db-version <software-version>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(universeName) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to upgrade\n", formatter.RedColor))
		}

		ybdbVersion, err := cmd.Flags().GetString("yb-db-version")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(ybdbVersion) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize(
					"No YugabyteDB software version found to upgrade\n",
					formatter.RedColor,
				))
		}

		// universeutil.Validations before software upgrade operation
		skipValidations, err := cmd.Flags().GetBool("skip-validations")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !skipValidations {
			_, universe, err := universeutil.Validations(cmd, util.UpgradeOperation)
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}

			isValidVersion, err := util.IsYBVersion(ybdbVersion)
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
			}
			if !isValidVersion {
				logrus.Fatal(
					formatter.Colorize(
						fmt.Sprintf(
							"%s is not a valid Yugbayte version string\n",
							ybdbVersion),
						formatter.RedColor,
					),
				)
			}

			universeDetails := universe.GetUniverseDetails()
			clusters := universeDetails.GetClusters()
			var oldYBDBVersion string
			if len(clusters) > 0 {
				primaryCluster, _ := universeutil.MustFindPrimaryCluster(
					clusters, universeName, universe.GetUniverseUUID())
				userIntent := primaryCluster.GetUserIntent()
				oldYBDBVersion = userIntent.GetYbSoftwareVersion()
			}
			err = util.ConfirmCommand(
				fmt.Sprintf("Are you sure you want to upgrade %s: %s from version %s to version %s",
					util.UniverseType, universeName, oldYBDBVersion, ybdbVersion),
				viper.GetBool("force"))
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			return
		}
		err = util.ConfirmCommand(
			fmt.Sprintf("Are you sure you want to upgrade %s: %s to version %s",
				util.UniverseType, universeName, ybdbVersion),
			viper.GetBool("force"))
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI, universe, err := universeutil.Validations(cmd, util.UpgradeOperation)
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		ybdbVersion, err := cmd.Flags().GetString("yb-db-version")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		universeName := universe.GetName()
		universeUUID := universe.GetUniverseUUID()
		universeDetails := universe.GetUniverseDetails()
		clusters := universeDetails.GetClusters()
		if len(clusters) < 1 {
			logrus.Fatalln(
				formatter.Colorize(
					"No clusters found in universe "+
						universeName+" ("+universeUUID+")\n",
					formatter.RedColor),
			)
		}
		var oldYBDBVersion string
		primaryCluster, _ := universeutil.MustFindPrimaryCluster(
			clusters, universeName, universeUUID)
		userIntent := primaryCluster.GetUserIntent()
		oldYBDBVersion = userIntent.GetYbSoftwareVersion()
		upgradeOption, err := cmd.Flags().GetString("upgrade-option")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		upgradeSysCatalog, err := cmd.Flags().GetBool("upgrade-system-catalog")
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

		req := ybaclient.SoftwareUpgradeParams{
			YbSoftwareVersion:              ybdbVersion,
			Clusters:                       clusters,
			UpgradeOption:                  upgradeOption,
			UpgradeSystemCatalog:           upgradeSysCatalog,
			SleepAfterTServerRestartMillis: tserverDelay,
			SleepAfterMasterRestartMillis:  masterDelay,
		}

		// Preview only, with finalize and rollback; without it, finalize in the same task.
		allowRollback := false
		if cmd.Flags().Lookup("allow-rollback") != nil {
			allowRollback, err = cmd.Flags().GetBool("allow-rollback")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}

		// Only the db_version API stops in PreFinalize; the software API finalizes in the task.
		var rUpgrade *ybaclient.YBPTask
		var response *http.Response
		if allowRollback {
			rUpgrade, response, err = authAPI.UpgradeDBVersion(universeUUID).
				SoftwareUpgradeParams(req).Execute()
		} else {
			rUpgrade, response, err = authAPI.UpgradeSoftware(universeUUID).
				SoftwareUpgradeParams(req).Execute()
		}
		if err != nil {
			util.FatalHTTPError(response, err, "Universe", "Upgrade Software")
		}

		logrus.Info(
			fmt.Sprintf("Upgrading universe %s (%s) from version %s to %s\n",
				formatter.Colorize(universeName, formatter.GreenColor),
				universeUUID,
				oldYBDBVersion, ybdbVersion))

		universeutil.WaitForUpgradeUniverseTask(authAPI, universeName, rUpgrade)
		if allowRollback && viper.GetBool("wait") {
			printPreFinalizeHint(authAPI, universeName, rUpgrade.GetResourceUUID())
		}
	},
}

// printPreFinalizeHint tells the user the next step when the upgrade stopped in PreFinalize.
// The upgrade already succeeded, so a lookup error only warns.
func printPreFinalizeHint(
	authAPI *ybaAuthClient.AuthAPIClient, universeName, universeUUID string) {
	universe, _, err := authAPI.GetUniverse(universeUUID).Execute()
	if err != nil {
		logrus.Warnf("Could not read the software upgrade state: %s\n", err.Error())
		return
	}
	details := universe.GetUniverseDetails()
	if details.GetSoftwareUpgradeState() == preFinalizeState {
		logrus.Info(formatter.Colorize(fmt.Sprintf(
			"Universe %s is in PreFinalize state. Run \"yba universe upgrade finalize -n %s\" "+
				"to complete the upgrade, or \"yba universe upgrade rollback -n %s\" "+
				"to return to the previous version.\n", universeName, universeName, universeName),
			formatter.YellowColor))
	}
}

func init() {
	upgradeSoftwareCmd.Flags().SortFlags = false

	upgradeSoftwareCmd.Flags().String("yb-db-version", "",
		"[Required] Target YugabyteDB software version.")
	upgradeSoftwareCmd.MarkFlagRequired("yb-db-version")

	upgradeSoftwareCmd.Flags().String("upgrade-option", "Rolling",
		"[Optional] Upgrade Options, defaults to Rolling. "+
			"Allowed values (case sensitive): Rolling, Non-Rolling (involves DB downtime)")
	upgradeSoftwareCmd.Flags().Bool("upgrade-system-catalog", true,
		"[Optional] Upgrade System Catalog after software upgrade, defaults to true.")
	upgradeSoftwareCmd.Flags().Int32("delay-between-master-servers",
		18000, "[Optional] Upgrade delay between Master servers (in miliseconds).")
	upgradeSoftwareCmd.Flags().Int32("delay-between-tservers",
		18000, "[Optional] Upgrade delay between Tservers (in miliseconds).")
	previewFlags := pflag.NewFlagSet("preview", pflag.ContinueOnError)
	previewFlags.Bool("allow-rollback", true,
		"[Optional] Keep the upgrade in PreFinalize state when the release needs a finalize, "+
			"so that it can be rolled back. Then run \"yba universe upgrade finalize\" or "+
			"\"yba universe upgrade rollback\". Defaults to true, same as the UI. "+
			"Set to false to finalize in the same task.")
	util.PreviewFlag(upgradeSoftwareCmd, previewFlags, []string{"allow-rollback"})
	if util.IsFeatureFlagEnabled(util.PREVIEW) {
		upgradeSoftwareCmd.Long += upgradeFlowHelp +
			"\nSet --allow-rollback=false to finalize in the same task."
	}
}
