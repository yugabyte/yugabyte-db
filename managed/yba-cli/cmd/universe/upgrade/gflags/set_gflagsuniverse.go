/*
 * Copyright (c) YugabyteDB, Inc.
 */

package gflags

import (
	"encoding/json"
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
)

// setGflagsUniverseCmd represents the universe set gflags command
var setGflagsUniverseCmd = &cobra.Command{
	Use:   "set",
	Short: "Set gflags for a YugabyteDB Anywhere Universe",
	Long: "Set gflags for a YugabyteDB Anywhere Universe. Refer " +
		"to https://github.com/yugabyte/yugabyte-db/blob/master/managed/yba-cli/templates " +
		"for structure of specific gflags file.",
	Example: `yba universe upgrade gflags set -n <universe-name> \
	--specific-gflags-file-path <file-path>`,
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

		specificGFlagsString, err := cmd.Flags().GetString("specific-gflags")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(specificGFlagsString) {
			filePath, err := cmd.Flags().GetString("specific-gflags-file-path")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if util.IsEmptyString(filePath) {
				logrus.Fatalln(
					formatter.Colorize(
						"No specific gflags found to upgrade. "+
							"Provide specific-gflags or specific-gflags-file-path\n",
						formatter.RedColor))
			}
		}

		// universeutil.Validations before gflags upgrade operation
		skipValidations, err := cmd.Flags().GetBool("skip-validations")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !skipValidations {
			_, _, err := universeutil.Validations(cmd, util.UpgradeOperation)
			if err != nil {
				logrus.Fatalf(
					formatter.Colorize(err.Error()+"\n", formatter.RedColor),
				)
			}

		}
		dryRun, err := cmd.Flags().GetBool("dry-run")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !dryRun {
			err = util.ConfirmCommand(
				fmt.Sprintf("Are you sure you want to upgrade Gflags %s: %s",
					util.UniverseType, universeName),
				viper.GetBool("force"))
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI, universe, err := universeutil.Validations(cmd, util.UpgradeOperation)
		if err != nil {
			logrus.Fatalf(
				formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		universeName := universe.GetName()
		universeUUID := universe.GetUniverseUUID()
		universeDetails := universe.GetUniverseDetails()
		clusters := universeDetails.GetClusters()
		if len(clusters) < 1 {
			logrus.Fatalln(formatter.Colorize(
				"No clusters found in universe "+universeName+" ("+universeUUID+")\n",
				formatter.RedColor,
			))
		}

		primaryCluster := universeutil.FindClusterByType(clusters, util.PrimaryClusterType)
		if universeutil.IsClusterEmpty(primaryCluster) {
			err := fmt.Errorf(
				"No primary cluster found in universe " + universeName + " (" + universeUUID + ")\n",
			)
			logrus.Fatal(formatter.Colorize(err.Error(), formatter.RedColor))
		}

		primaryUserIntent := primaryCluster.GetUserIntent()
		version := primaryUserIntent.GetYbSoftwareVersion()

		var cliSpecificGFlags []util.SpecificGFlagsCLIOutput

		specificGFlagsString, err := cmd.Flags().GetString("specific-gflags")
		if err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(specificGFlagsString) {
			filePath, err := cmd.Flags().GetString("specific-gflags-file-path")
			if err != nil {
				logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if !util.IsEmptyString(filePath) {
				fileByte, err := os.ReadFile(filePath)
				if err != nil {
					logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
				}
				specificGFlagsString = string(fileByte)
			}
		}

		err = json.Unmarshal([]byte(specificGFlagsString), &cliSpecificGFlags)
		if err != nil {
			logrus.Fatal(
				formatter.Colorize(
					"Error unmarshaling specific gflags: "+err.Error()+"\n",
					formatter.RedColor))
		}

		skipValidations, err := cmd.Flags().GetBool("skip-validations")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if !skipValidations {
			errors := validateGFlags(
				authAPI,
				universeName,
				universeUUID,
				version,
				cliSpecificGFlags,
			)
			if len(errors) > 0 {
				errMessage := "Error validating gflags:"
				for _, err := range errors {
					errMessage = fmt.Sprintf("%s\n%s", errMessage, err)
				}
				errMessage = fmt.Sprintf(
					"%s\nPlease review them and try again. "+
						"If you still wish to proceed, you can bypass validations by adding the --skip-validations flag.\n",
					errMessage,
				)
				logrus.Fatal(formatter.Colorize(errMessage, formatter.RedColor))
			}
		}

		dryRun, err := cmd.Flags().GetBool("dry-run")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		if dryRun {
			logrus.Infof(
				"Dry run for universe %s (%s) gflags is complete\n",
				formatter.Colorize(universeName, formatter.GreenColor),
				universeUUID,
			)
			return
		}

		specificGFlags := make([]ybaclient.SpecificGFlags, 0)
		for _, gflagStructure := range cliSpecificGFlags {
			gflags := gflagStructure.SpecificGFlags
			specificGFlag := ybaclient.SpecificGFlags{
				InheritFromPrimary: gflags.InheritFromPrimary,
				PerProcessFlags: &ybaclient.PerProcessFlags{
					Value: *gflags.PerProcessFlags,
				},
			}

			perAzMap := make(map[string]ybaclient.PerProcessFlags, 0)
			for k, v := range *gflags.PerAZ {
				perAzMap[k] = ybaclient.PerProcessFlags{
					Value: v,
				}
			}
			specificGFlag.PerAZ = &perAzMap

			specificGFlags = append(specificGFlags, specificGFlag)
		}

		for _, gflags := range cliSpecificGFlags {
			isClusterInRequestBodyWithValidUUID := false
			for i := 0; i < len(clusters); i++ {
				clusterUserIntent := clusters[i].GetUserIntent()
				clusterType := clusters[i].GetClusterType()
				clusterUUID := clusters[i].GetUuid()
				if strings.Compare(
					strings.ToUpper(clusterType),
					strings.ToUpper(gflags.ClusterType),
				) == 0 {
					if strings.Compare(clusterUUID, gflags.ClusterUUID) == 0 {
						logrus.Debugf(
							"Updating specific gflags for cluster %s (%s)\n",
							clusterUUID,
							clusterType)
						clusterUserIntent.SetSpecificGFlags(specificGFlags[i])
						clusters[i].SetUserIntent(clusterUserIntent)
						isClusterInRequestBodyWithValidUUID = true
					}
				}
			}
			if !isClusterInRequestBodyWithValidUUID {
				logrus.Fatal(
					formatter.Colorize(
						"Cluster "+gflags.ClusterUUID+" ("+gflags.ClusterType+
							")"+" not found in universe\n",
						formatter.RedColor,
					))
			}
		}

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

		req := ybaclient.GFlagsUpgradeParams{
			Clusters:                       clusters,
			UpgradeOption:                  upgradeOption,
			SleepAfterTServerRestartMillis: tserverDelay,
			SleepAfterMasterRestartMillis:  masterDelay,
		}

		rUpgrade, response, err := authAPI.UpgradeGFlags(universeUUID).
			GflagsUpgradeParams(req).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Universe", "Upgrade GFlags")
		}

		logrus.Info(
			fmt.Sprintf("Upgrading universe %s (%s) gflags\n",
				formatter.Colorize(universeName, formatter.GreenColor),
				universeUUID,
			))

		universeutil.WaitForUpgradeUniverseTask(authAPI, universeName, rUpgrade)

	},
}

func init() {
	setGflagsUniverseCmd.Flags().SortFlags = false

	setGflagsUniverseCmd.Flags().String("specific-gflags", "",
		fmt.Sprintf(
			"[Optional] Specific gflags to be set. "+
				"Use the modified output of \"yba universe upgrade gflags get\" command "+
				"as the flag value. Quote the string with single quotes. %s",
			formatter.Colorize("Provide either specific-gflags or specific-gflags-file-path",
				formatter.GreenColor)))

	setGflagsUniverseCmd.Flags().String("specific-gflags-file-path", "",
		fmt.Sprintf(
			"[Optional] Path to modified json output file of"+
				" \"yba universe upgrade gflags get\" command. %s",
			formatter.Colorize(
				"Provide either specific-gflags or specific-gflags-file-path",
				formatter.GreenColor),
		))

	setGflagsUniverseCmd.MarkFlagsMutuallyExclusive("specific-gflags", "specific-gflags-file-path")

	setGflagsUniverseCmd.Flags().String("upgrade-option", "Rolling",
		"[Optional] Upgrade Options, defaults to Rolling. "+
			"Allowed values (case sensitive): Rolling, Non-Rolling (involves DB downtime), Non-Restart")
	setGflagsUniverseCmd.Flags().Int32("delay-between-master-servers",
		18000, "[Optional] Upgrade delay between Master servers (in miliseconds).")
	setGflagsUniverseCmd.Flags().Int32("delay-between-tservers",
		18000, "[Optional] Upgrade delay between Tservers (in miliseconds).")

	setGflagsUniverseCmd.Flags().Bool("dry-run", false,
		"[Optional] Only validate the input and do not actually set the GFlags.")

}
