/*
 * Copyright (c) YugabyteDB, Inc.
 */

package telemetryconfig

import (
	"os"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/universe/telemetryconfig"
)

// getTelemetryConfigCmd represents the universe export-telemetry-configs get command
var getTelemetryConfigCmd = &cobra.Command{
	Use:   "get",
	Short: "Get the telemetry export configuration of a YugabyteDB Anywhere Universe",
	Long: "Get the telemetry export configuration of a YugabyteDB Anywhere Universe. " +
		"An unconfigured universe returns an empty configuration. " +
		"Allowed output format: json, pretty",
	Example: `yba universe upgrade export-telemetry-configs get -n <universe-name>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(universeName) == 0 {
			cmd.Help()
			logrus.Fatalln(formatter.Colorize(
				"No universe name found to get telemetry configuration\n", formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI, universe, err := Validations(cmd)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		universeUUID := universe.GetUniverseUUID()

		r, response, err := authAPI.GetExportTelemetryConfig(universeUUID).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Universe", "Get Telemetry Config")
		}

		output := viper.GetString("output")
		if util.IsOutputType(formatter.TableFormatKey) {
			output = formatter.JSONFormatKey
		}

		telemetryConfigCtx := formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format:  telemetryconfig.NewTelemetryConfigFormat(output),
		}

		telemetryconfig.Write(telemetryConfigCtx, r)
	},
}

func init() {
	getTelemetryConfigCmd.Flags().SortFlags = false
}
