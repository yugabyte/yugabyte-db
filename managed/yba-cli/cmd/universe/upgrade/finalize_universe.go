/*
 * Copyright (c) YugabyteDB, Inc.
 */

package upgrade

import (
	"fmt"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/universeutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// upgradeFinalizeCmd represents the universe upgrade finalize command
var upgradeFinalizeCmd = &cobra.Command{
	Use:   "finalize",
	Short: "Finalize the software upgrade of a YugabyteDB Anywhere Universe",
	Long: "Finalize the software upgrade of a YugabyteDB Anywhere Universe. " +
		"Run it when the universe is in PreFinalize or FinalizeFailed state. " +
		"After finalize, the upgrade cannot be rolled back." + upgradeFlowHelp,
	Example: `yba universe upgrade finalize --name <universe-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		universeName := util.MustGetFlagString(cmd, "name")

		skipValidations := util.MustGetFlagBool(cmd, "skip-validations")
		prompt := fmt.Sprintf("Are you sure you want to finalize the software upgrade of %s: %s",
			util.UniverseType, universeName)
		if !skipValidations {
			authAPI, universe, err := universeutil.Validations(cmd, util.UpgradeOperation)
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			details := universe.GetUniverseDetails()
			state := details.GetSoftwareUpgradeState()
			if state != preFinalizeState && state != finalizeFailedState {
				logrus.Fatal(formatter.Colorize(fmt.Sprintf(
					"Cannot finalize: universe %s is in software upgrade state %s, "+
						"expected PreFinalize or FinalizeFailed\n", universeName, state),
					formatter.RedColor))
			}
			// The xCluster list only adds context to the prompt, so a lookup error must not block.
			info, _, err := authAPI.PreFinalizeSoftwareUpgradeInfo(
				universe.GetUniverseUUID()).Execute()
			if err != nil {
				logrus.Warnf("Could not list xCluster universes affected by finalize: %s\n",
					err.Error())
			} else if impacted := impactedUniverses(info); impacted != "" {
				prompt = prompt + ". Finalize also affects the xCluster universes: " + impacted
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

		upgradeSysCatalog := util.MustGetFlagBool(cmd, "upgrade-system-catalog")

		req := ybaclient.FinalizeUpgradeParams{
			Clusters: universe.GetUniverseDetails().Clusters,
			// Finalize restarts no nodes; Rolling is the same default the UI gets.
			UpgradeOption:        "Rolling",
			UpgradeSystemCatalog: upgradeSysCatalog,
		}

		rTask, response, err := authAPI.FinalizeUpgrade(universeUUID).
			FinalizeUpgradeParams(req).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Universe", "Finalize Upgrade")
		}

		logrus.Info(
			fmt.Sprintf("Finalizing software upgrade of universe %s (%s)\n",
				formatter.Colorize(universeName, formatter.GreenColor), universeUUID))

		universeutil.WaitForUpgradeUniverseTask(authAPI, universeName, rTask)
	},
}

// impactedUniverses lists the xCluster universes that finalize affects, as "name (version)".
func impactedUniverses(info *ybaclient.FinalizeUpgradeInfoResponse) string {
	if info == nil {
		return ""
	}
	names := make([]string, 0)
	for _, u := range info.GetImpactedXClusterConnectedUniverse() {
		names = append(names, fmt.Sprintf("%s (%s)", u.GetUniverseName(), u.GetYbSoftwareVersion()))
	}
	return strings.Join(names, ", ")
}

func init() {
	upgradeFinalizeCmd.Flags().SortFlags = false

	upgradeFinalizeCmd.Flags().Bool("upgrade-system-catalog", true,
		"[Optional] Upgrade the YSQL system catalog during finalize, defaults to true.")
}
