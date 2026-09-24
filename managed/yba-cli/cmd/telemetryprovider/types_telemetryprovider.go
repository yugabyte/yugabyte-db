/*
 * Copyright (c) YugabyteDB, Inc.
 */

package telemetryprovider

import (
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/telemetryprovidertype"
)

// typesTelemetryProviderCmd lists the provider types this YBA supports. A type allowed for
// logs is not necessarily allowed for metrics.
var typesTelemetryProviderCmd = &cobra.Command{
	Use:     "types",
	Aliases: []string{"sinks"},
	GroupID: "action",
	Short:   "List available YugabyteDB Anywhere telemetry provider types",
	Long: "List the telemetry provider types supported by this YugabyteDB Anywhere, " +
		"and whether each can export logs and metrics",
	Example: `yba telemetry-provider types --export-type metrics`,
	PreRun: func(cmd *cobra.Command, args []string) {
		exportType, err := cmd.Flags().GetString("export-type")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(exportType) {
			switch strings.ToLower(exportType) {
			case "logs", "metrics":
			default:
				logrus.Fatalf(formatter.Colorize("Invalid export-type specified. "+
					"Allowed values: logs, metrics\n", formatter.RedColor))
			}
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		allowed, version, err := authAPI.TelemetryProviderTypesYBAVersionCheck()
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !allowed {
			logrus.Fatalf(formatter.Colorize(
				"Listing telemetry provider types is not supported by YBA version "+version+"\n",
				formatter.RedColor))
		}

		exportType, err := cmd.Flags().GetString("export-type")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		request := authAPI.ListTelemetryProviderTypes()
		if !util.IsEmptyString(exportType) {
			request = request.ExportType(strings.ToLower(exportType))
		}
		r, response, err := request.Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Telemetry Provider", "List Types")
		}

		if len(r) < 1 {
			if util.IsOutputType(formatter.TableFormatKey) {
				logrus.Info("No telemetry provider types found\n")
			} else {
				logrus.Info("[]\n")
			}
			return
		}

		typesCtx := formatter.Context{
			Command: "list",
			Output:  os.Stdout,
			Format: telemetryprovidertype.NewTelemetryProviderTypeFormat(
				viper.GetString("output")),
		}
		telemetryprovidertype.Write(typesCtx, r)
	},
}

func init() {
	typesTelemetryProviderCmd.Flags().SortFlags = false
	typesTelemetryProviderCmd.Flags().String("export-type", "",
		"[Optional] Filter by export type. Allowed values (case insensitive): logs, metrics.")
}
