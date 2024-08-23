/*
 * Copyright (c) YugaByte, Inc.
 */

package upgrade

import (
	"fmt"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// upgradeVMImageCmd represents the universe upgrade vm image command
var upgradeVMImageCmd = &cobra.Command{
	Use:     "linux-os",
	Aliases: []string{"vm-image", "os"},
	Short:   "VM Linux OS patch for a YugabyteDB Anywhere Universe",
	Long: "VM Linux OS patch for a YugabyteDB Anywhere Universe. Supported only for universes" +
		" of cloud type AWS, GCP and Azure.",
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

		// Validations before vm image upgrade operation
		skipValidations, err := cmd.Flags().GetBool("skip-validations")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !skipValidations {
			_, _, err := Validations(cmd, util.UpgradeOperation)
			if err != nil {
				logrus.Fatalf(
					formatter.Colorize(err.Error()+"\n", formatter.RedColor),
				)
			}

		}
		err = util.ConfirmCommand(
			fmt.Sprintf("Are you sure you want to upgrade linux OS for %s: %s",
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

		primaryLinuxVersionName, err := cmd.Flags().GetString("primary-linux-version")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		imageBundleInfo := make([]ybaclient.ImageBundleUpgradeInfo, 0)

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

		primaryCluster := clusters[0]
		primaryUserIntent := primaryCluster.GetUserIntent()

		provider, response, err := authAPI.GetProvider(primaryUserIntent.GetProvider()).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err,
				"Universe", "Upgrade Linux Version - Get Provider")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		var primaryResultImageBundle string
		providerImageBundles := provider.GetImageBundles()
		if len(providerImageBundles) == 0 {
			err := fmt.Errorf("no image bundles found for provider %s", provider.GetName())
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		for _, im := range providerImageBundles {
			if strings.Compare(im.GetName(), primaryLinuxVersionName) == 0 {
				imageBundleInfo = append(imageBundleInfo, ybaclient.ImageBundleUpgradeInfo{
					ClusterUuid:     primaryCluster.GetUuid(),
					ImageBundleUuid: im.GetUuid(),
				})
				primaryResultImageBundle = im.GetUuid()
			}
		}

		if len(imageBundleInfo) == 0 {
			logrus.Fatalf(
				formatter.Colorize(
					"The provided linux version name cannot be found\n", formatter.RedColor))
		}

		if len(clusters) > 1 {
			inheritFromPrimary, err := cmd.Flags().GetBool("inherit-from-primary")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			rrCluster := clusters[1]
			rrImageBundle := ""
			if !inheritFromPrimary {
				rrLinuxVersionName, err := cmd.Flags().GetString("rr-linux-version")
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
				for _, im := range providerImageBundles {
					if strings.Compare(im.GetName(), rrLinuxVersionName) == 0 {
						rrImageBundle = im.GetUuid()
					}
				}
				if len(rrImageBundle) == 0 {
					logrus.Fatalf(
						formatter.Colorize(
							"The provided linux version name cannot be found\n",
							formatter.RedColor))
				}
			} else {
				rrImageBundle = primaryResultImageBundle
			}
			imageBundleInfo = append(imageBundleInfo, ybaclient.ImageBundleUpgradeInfo{
				ClusterUuid:     rrCluster.GetUuid(),
				ImageBundleUuid: rrImageBundle,
			})
		}

		masterDelay, err := cmd.Flags().GetInt32("delay-between-master-servers")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		tserverDelay, err := cmd.Flags().GetInt32("delay-between-tservers")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		req := ybaclient.VMImageUpgradeParams{
			ImageBundles:                   &imageBundleInfo,
			Clusters:                       clusters,
			UpgradeOption:                  "Rolling",
			SleepAfterTServerRestartMillis: tserverDelay,
			SleepAfterMasterRestartMillis:  masterDelay,
		}

		rUpgrade, response, err := authAPI.UpgradeVMImage(universeUUID).
			VmimageUpgradeParams(req).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Universe",
				"Upgrade Linux Version")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}

		taskUUID := rUpgrade.GetTaskUUID()
		logrus.Info(
			fmt.Sprintf("Patching universe %s (%s) linux OS\n",
				formatter.Colorize(universeName, formatter.GreenColor),
				universeUUID,
			))

		waitForUpgradeUniverseTask(authAPI, universeName, universeUUID, taskUUID)
	},
}

func init() {
	upgradeVMImageCmd.Flags().SortFlags = false

	upgradeVMImageCmd.Flags().String("primary-linux-version", "",
		"[Required] Primary cluster linux OS version name to be applied.")
	upgradeVMImageCmd.MarkFlagRequired("primary-linux-version")
	upgradeVMImageCmd.Flags().Bool("inherit-from-primary", true,
		"[Optional] Apply the same linux OS version of primary cluster to read replica cluster.")
	upgradeVMImageCmd.Flags().String("rr-linux-version", "",
		"[Optional] Read replica cluster linux OS version name to be applied. "+
			"Ignored if inherit-from-primary is set to true.")

	upgradeVMImageCmd.Flags().Int32("delay-between-master-servers",
		18000, "[Optional] Upgrade delay between Master servers (in miliseconds).")
	upgradeVMImageCmd.Flags().Int32("delay-between-tservers",
		18000, "[Optional] Upgrade delay between Tservers (in miliseconds).")

}
