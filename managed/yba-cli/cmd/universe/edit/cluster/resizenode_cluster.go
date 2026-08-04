/*
 * Copyright (c) YugabyteDB, Inc.
 */

package cluster

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

// resizeNodeCmd is for changing volume size for nodes in the primary cluster
var resizeNodeCmd = &cobra.Command{
	Use:     "smart-resize",
	Aliases: []string{"resize-node"},
	Short: "Edit the Volume size or instance type of " +
		"Primary Cluster nodes in a YugabyteDB Anywhere universe using smart resize.",
	Long: "Edit the volume size or instance type of Primary Cluster nodes " +
		"in a YugabyteDB Anywhere universe using smart resize.",
	Example: `yba universe edit cluster smart-resize \
  -n <universe-name> --volume-size <volume-size>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(universeName) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to edit\n", formatter.RedColor))
		}

		// Validations before upgrade operation
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
			fmt.Sprintf("Are you sure you want to trigger "+
				"smart resize of primary cluster nodes %s: %s",
				"universe", universeName),
			viper.GetBool("force"))
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI, universe, err := universeutil.Validations(cmd, util.EditOperation)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		universeUUID := universe.GetUniverseUUID()
		universeName := universe.GetName()
		universeDetails := universe.GetUniverseDetails()
		clusters := universeDetails.GetClusters()
		if len(clusters) < 1 {
			err := fmt.Errorf(
				"No clusters found in universe " + universeName + " (" + universeUUID + ")\n")
			logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
		}

		cluster := universeutil.FindClusterByType(clusters, util.PrimaryClusterType)

		if universeutil.IsClusterEmpty(cluster) {
			err := fmt.Errorf(
				"No primary cluster found in universe " + universeName + " (" + universeUUID + ")\n",
			)
			logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
		}

		userIntent := cluster.GetUserIntent()
		deviceInfo := userIntent.GetDeviceInfo()

		dedicatedNodes := userIntent.GetDedicatedNodes()
		if dedicatedNodes {
			masterDeviceInfo := userIntent.GetMasterDeviceInfo()
			if cmd.Flags().Changed("dedicated-master-volume-size") {
				masterVolumeSize, err := cmd.Flags().GetInt("dedicated-master-volume-size")
				if err != nil {
					logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
				masterDeviceInfo.SetVolumeSize(int32(masterVolumeSize))
				userIntent.SetMasterDeviceInfo(masterDeviceInfo)
			}
			instanceType, err := cmd.Flags().GetString("dedicated-master-instance-type")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if !util.IsEmptyString(instanceType) {
				userIntent.SetMasterInstanceType(instanceType)
			}
		}

		if cmd.Flags().Changed("volume-size") {
			volumeSize, err := cmd.Flags().GetInt("volume-size")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			deviceInfo.SetVolumeSize(int32(volumeSize))
		}

		instanceType, err := cmd.Flags().GetString("instance-type")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(instanceType) {
			userIntent.SetInstanceType(instanceType)
		}

		userIntent.SetDeviceInfo(deviceInfo)
		cluster.SetUserIntent(userIntent)
		clusters[0] = cluster

		masterDelay, err := cmd.Flags().GetInt32("delay-between-master-servers")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		tserverDelay, err := cmd.Flags().GetInt32("delay-between-tservers")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		req := ybaclient.ResizeNodeParams{
			Clusters:                       clusters,
			UniverseUUID:                   util.GetStringPointer(universeUUID),
			UpgradeOption:                  "Rolling",
			SleepAfterTServerRestartMillis: tserverDelay,
			SleepAfterMasterRestartMillis:  masterDelay,
		}

		rEdit, response, err := authAPI.ResizeNode(universeUUID).
			ResizeNodeParams(req).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Universe", "Edit Volume Size of Primary Cluster")
		}

		waitForEditClusterTask(
			authAPI,
			universe.GetName(),
			universe.GetUniverseUUID(),
			rEdit)

	},
}

func init() {
	resizeNodeCmd.Flags().SortFlags = false

	resizeNodeCmd.Flags().String("instance-type", "",
		"[Optional] Instance Type for the universe nodes.")
	resizeNodeCmd.Flags().Int("volume-size", 0,
		"[Optional] The size of each volume in each instance.")

	resizeNodeCmd.Flags().String("dedicated-master-instance-type", "",
		"[Optional] Instance Type for the each master instance. "+
			"Applied only when universe has dedicated master nodes.")
	// if dedicated nodes is set to true
	resizeNodeCmd.Flags().Int("dedicated-master-volume-size", 0,
		"[Optional] The size of each volume in each master instance. "+
			"Applied only when universe has dedicated master nodes.")

	resizeNodeCmd.Flags().Int32("delay-between-master-servers",
		18000, "[Optional] Upgrade delay between Master servers (in miliseconds).")
	resizeNodeCmd.Flags().Int32("delay-between-tservers",
		18000, "[Optional] Upgrade delay between Tservers (in miliseconds).")

}
