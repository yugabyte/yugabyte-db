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
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// upgradeSoftwareCmd represents the universe upgrade software command
var upgradeSoftwareCmd = &cobra.Command{
	Use:   "software",
	Short: "Software upgrade for a YugabyteDB Anywhere Universe",
	Long:  "Software upgrade for a YugabyteDB Anywhere Universe",
	PreRun: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(universeName) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to upgrade\n", formatter.RedColor))
		}

		ybdbVersion, err := cmd.Flags().GetString("yb-db-version")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(ybdbVersion) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize(
					"No YugabyteDB software version found to upgrade\n",
					formatter.RedColor,
				))
		}

		// Validations before software upgrade operation
		skipValidations, err := cmd.Flags().GetBool("skip-validations")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !skipValidations {
			universe, err := upgradeValidations(universeName)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}

			isValidVersion, err := util.IsYBVersion(ybdbVersion)
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
			}
			if !isValidVersion {
				logrus.Fatal(
					formatter.Colorize(
						fmt.Sprintf("%s is not a valid Yugbayte version string", ybdbVersion), formatter.RedColor,
					),
				)
			}

			universeDetails := universe.GetUniverseDetails()
			clusters := universeDetails.GetClusters()
			var oldYBDBVersion string
			if len(clusters) > 0 {
				userIntent := clusters[0].GetUserIntent()
				oldYBDBVersion = userIntent.GetYbSoftwareVersion()
			}
			err = util.ConfirmCommand(
				fmt.Sprintf("Are you sure you want to upgrade %s: %s from version %s to version %s",
					util.UniverseType, universeName, oldYBDBVersion, ybdbVersion),
				viper.GetBool("force"))
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
			}
			return
		}
		err = util.ConfirmCommand(
			fmt.Sprintf("Are you sure you want to upgrade %s: %s to version %s",
				util.UniverseType, universeName, ybdbVersion),
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
		ybdbVersion, err := cmd.Flags().GetString("yb-db-version")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		universeListRequest := authAPI.ListUniverses()
		universeListRequest = universeListRequest.Name(universeName)

		r, response, err := universeListRequest.Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err,
				"Universe", "Upgrade Software - Fetch Universes")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}
		if len(r) < 1 {
			fmt.Println("No universes found")
			return
		}
		universeUUID := r[0].GetUniverseUUID()
		universeDetails := r[0].GetUniverseDetails()
		clusters := universeDetails.GetClusters()
		var oldYBDBVersion string
		if len(clusters) > 0 {
			userIntent := clusters[0].GetUserIntent()
			oldYBDBVersion = userIntent.GetYbSoftwareVersion()
		}

		upgradeOption, err := cmd.Flags().GetString("upgrade-option")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
		}

		upgradeSysCatalog, err := cmd.Flags().GetBool("upgrade-system-catalog")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
		}

		masterDelay, err := cmd.Flags().GetInt32("delay-between-master-servers")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
		}

		tserverDelay, err := cmd.Flags().GetInt32("delay-between-tservers")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
		}

		req := ybaclient.SoftwareUpgradeParams{
			YbSoftwareVersion:              ybdbVersion,
			Clusters:                       clusters,
			UpgradeOption:                  upgradeOption,
			UpgradeSystemCatalog:           upgradeSysCatalog,
			SleepAfterTServerRestartMillis: tserverDelay,
			SleepAfterMasterRestartMillis:  masterDelay,
		}

		rUpgrade, response, err := authAPI.UpgradeSoftware(universeUUID).
			SoftwareUpgradeParams(req).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Universe", "Upgrade Software")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		taskUUID := rUpgrade.GetTaskUUID()
		logrus.Info(
			fmt.Sprintf("Upgrading universe %s from version %s to %s\n",
				formatter.Colorize(universeName, formatter.GreenColor),
				oldYBDBVersion, ybdbVersion))

		waitForUpgradeUniverseTask(authAPI, universeName, universeUUID, taskUUID)
	},
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
}