/*
 * Copyright (c) YugabyteDB, Inc.
 */

package gflags

import (
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/universeutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/universe/gflags"
)

// getGflagsUniverseCmd represents the universe get gflags command
var getGflagsUniverseCmd = &cobra.Command{
	Use:   "get",
	Short: "Get gflags for a YugabyteDB Anywhere Universe",
	Long:  "Get gflags for a YugabyteDB Anywhere Universe. Allowed output format: json, pretty",
	PreRun: func(cmd *cobra.Command, args []string) {
		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(universeName) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to get gflags\n", formatter.RedColor))
		}

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
	},
	Run: func(cmd *cobra.Command, args []string) {
		_, universe, err := universeutil.Validations(cmd, util.UpgradeOperation)
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

		r := make([]util.SpecificGFlagsCLIOutput, 0)

		for i := 0; i < len(clusters); i++ {
			clusterUserIntent := clusters[i].GetUserIntent()
			clusterSpecificGflags := clusterUserIntent.GetSpecificGFlags()

			clusterType := clusters[i].GetClusterType()
			clusterUUID := clusters[i].GetUuid()

			perProccessFlags := clusterSpecificGflags.GetPerProcessFlags()
			perProcessFlagValues := util.PerProcessFlags(perProccessFlags.GetValue())

			specificGflags := util.SpecificGFlagsCLI{
				InheritFromPrimary: clusterSpecificGflags.InheritFromPrimary,
				PerProcessFlags:    &(perProcessFlagValues),
			}

			perAZMap := make(map[string]util.PerProcessFlags, 0)

			for k, v := range clusterSpecificGflags.GetPerAZ() {
				perProcessFlagValues := util.PerProcessFlags(v.GetValue())
				perAZMap[k] = perProcessFlagValues
			}

			specificGflags.PerAZ = &perAZMap

			cliOutput := util.SpecificGFlagsCLIOutput{
				SpecificGFlags: specificGflags,
				ClusterType:    clusterType,
				ClusterUUID:    clusterUUID,
			}
			r = append(r, cliOutput)
		}

		output := viper.GetString("output")
		if util.IsOutputType(formatter.TableFormatKey) {
			output = formatter.JSONFormatKey
		}

		universesCtx := formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  gflags.NewGFlagsUniverseFormat(output),
		}

		gflags.Write(universesCtx, r)

	},
}
