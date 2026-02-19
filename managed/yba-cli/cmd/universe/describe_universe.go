/*
 * Copyright (c) YugabyteDB, Inc.
 */

package universe

import (
	"fmt"
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/universeutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/universe"
)

var describeUniverseCmd = &cobra.Command{
	Use:     "describe",
	Aliases: []string{"get"},
	GroupID: "action",
	Short:   "Describe a YugabyteDB Anywhere universe",
	Long:    "Describe a universe in YugabyteDB Anywhere",
	Example: `yba universe describe --name <universe-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		universeNameFlag, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(universeNameFlag) == 0 {
			cmd.Help()
			logrus.Fatalln(
				formatter.Colorize("No universe name found to describe\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		universeListRequest := authAPI.ListUniverses()
		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		universeListRequest = universeListRequest.Name(universeName)

		r, response, err := universeListRequest.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Universe", "Describe")
		}

		if len(r) < 1 {
			logrus.Fatalf(
				formatter.Colorize(
					fmt.Sprintf("No universes with name: %s found\n", universeName),
					formatter.RedColor,
				))
		}

		universe.Certificates, response, err = authAPI.GetListOfCertificates().Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Universe", "Describe - Get Certificates")
		}

		universe.Providers, response, err = authAPI.GetListOfProviders().Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Universe", "Describe - Get Providers")
		}

		universe.KMSConfigs, err = authAPI.GetListOfKMSConfigs(
			"Universe", "Describe - Get KMS Configurations")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		outputType, err := cmd.Flags().GetString("output")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		outputType = strings.ToLower(outputType)
		if strings.Contains(outputType, "cli") {
			outputFormat := util.GetCLIOutputFormat(outputType)
			universeutil.PopulateDescribeUniverseCLIOutput(
				createUniverseCmd.Flags(),
				r[0],
				outputFormat,
			)
			return
		}
		viper.Set("output", outputType)

		if len(r) > 0 && util.IsOutputType(formatter.TableFormatKey) {
			fullUniverseContext := *universe.NewFullUniverseContext()
			fullUniverseContext.Output = os.Stdout
			fullUniverseContext.Format = universe.NewFullUniverseFormat(viper.GetString("output"))
			fullUniverseContext.SetFullUniverse(r[0])
			fullUniverseContext.Write()
			return
		}

		universeCtx := formatter.Context{
			Command: "describe",
			Output:  os.Stdout,
			Format:  universe.NewUniverseFormat(viper.GetString("output")),
		}
		universe.Write(universeCtx, r)

	},
}

func init() {
	describeUniverseCmd.Flags().SortFlags = false
	describeUniverseCmd.Flags().StringP("name", "n", "",
		"[Required] The name of the universe to be described.")
	describeUniverseCmd.MarkFlagRequired("name")

	describeUniverseCmd.Flags().StringP("output", "o", formatter.TableFormatKey,
		"[Optional] Select the desired output format. \"cli-\" outputs"+
			" can be used directly in \"yba universe create\" command. "+
			"Allowed values: table, json, pretty, cli-flag, cli-json, cli-yaml.")

}
