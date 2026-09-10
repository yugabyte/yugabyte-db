/*
 * Copyright (c) YugabyteDB, Inc.
 */

package telemetryconfig

import (
	"encoding/json"
	"fmt"
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	ybav2client "github.com/yugabyte/platform-go-client/v2"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/universe/universeutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// setTelemetryConfigCmd represents the universe export-telemetry-configs set command
var setTelemetryConfigCmd = &cobra.Command{
	Use:   "set",
	Short: "Set the telemetry export configuration of a YugabyteDB Anywhere Universe",
	Long: "Set the telemetry export configuration of a YugabyteDB Anywhere Universe. " +
		"The input replaces the whole configuration, so any export type left out is " +
		"disabled. Use the output of \"yba universe upgrade export-telemetry-configs get\" as the " +
		"starting point, and pass {} to disable every export. Each exporter_uuid accepts " +
		"either a telemetry provider UUID or its name. Refer to " +
		"https://github.com/yugabyte/yugabyte-db/blob/master/managed/yba-cli/templates " +
		"for the structure of the telemetry config file.",
	Example: `yba universe upgrade export-telemetry-configs set -n <universe-name> \
	--telemetry-config-file-path <file-path>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		viper.BindPFlag("force", cmd.Flags().Lookup("force"))
		universeName, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(universeName) == 0 {
			cmd.Help()
			logrus.Fatalln(formatter.Colorize(
				"No universe name found to set telemetry configuration\n", formatter.RedColor))
		}
		if util.IsEmptyString(readInlineConfigFlag(cmd)) &&
			util.IsEmptyString(readConfigFilePathFlag(cmd)) {
			logrus.Fatalln(formatter.Colorize(
				"No telemetry configuration found to set. Provide telemetry-config or "+
					"telemetry-config-file-path\n",
				formatter.RedColor))
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI, universe, err := Validations(cmd)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		universeName := universe.GetName()
		universeUUID := universe.GetUniverseUUID()

		requested := parseTelemetryConfig(cmd)

		// Resolve first, so every later check sees the document as it will be sent.
		resolved := ResolveExporters(authAPI, requested)
		ValidateServerLogsSupport(authAPI, requested)
		ValidateKubernetesSupport(requested, universe)
		ValidateExporters(authAPI, requested, resolved)
		ValidateOTLPAllowed(authAPI, resolved)

		current, response, err := authAPI.GetExportTelemetryConfig(universeUUID).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Universe", "Get Telemetry Config")
		}

		if IsSameConfig(current, requested) {
			logrus.Infof(
				"Telemetry export configuration of universe %s (%s) already matches the "+
					"requested configuration, nothing to apply\n",
				formatter.Colorize(universeName, formatter.GreenColor),
				universeUUID,
			)
			return
		}

		dryRun, err := cmd.Flags().GetBool("dry-run")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		logExporterResolution(requested, resolved)

		disabled := DisabledSections(current, requested)
		if dryRun {
			if len(disabled) > 0 {
				logrus.Infof(
					"Applying this configuration would disable export for: %s\n",
					formatter.Colorize(strings.Join(disabled, ", "), formatter.YellowColor))
			}
			logrus.Infof(
				"Dry run for universe %s (%s) telemetry configuration is complete\n",
				formatter.Colorize(universeName, formatter.GreenColor),
				universeUUID,
			)
			return
		}

		confirmMessage := fmt.Sprintf(
			"Are you sure you want to replace the telemetry export configuration of %s: %s",
			util.UniverseType, universeName)
		if len(disabled) > 0 {
			confirmMessage = fmt.Sprintf(
				"%s. This disables export for %s",
				confirmMessage, strings.Join(disabled, ", "))
		}
		if err := util.ConfirmCommand(confirmMessage, viper.GetBool("force")); err != nil {
			logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		requestBody := ybav2client.ExportTelemetryConfigSpec{
			TelemetryConfig: requested,
			UpgradeOptions:  buildUpgradeOptions(cmd),
		}

		rTask, response, err := authAPI.ConfigureExportTelemetryConfig(universeUUID).
			ExportTelemetryConfigSpec(requestBody).Execute()
		if err != nil {
			util.FatalHTTPError(response, err, "Universe", "Set Telemetry Config")
		}

		logrus.Info(fmt.Sprintf(
			"Configuring telemetry export for universe %s (%s)\n",
			formatter.Colorize(universeName, formatter.GreenColor),
			universeUUID,
		))

		// v2 returns a YBATask; the shared wait path takes the v1 YBPTask. Same fields.
		universeutil.WaitForUpgradeUniverseTask(authAPI, universeName, &ybaclient.YBPTask{
			ResourceUUID: rTask.ResourceUuid,
			TaskUUID:     rTask.TaskUuid,
		})
	},
}

func readInlineConfigFlag(cmd *cobra.Command) string {
	value, err := cmd.Flags().GetString("telemetry-config")
	if err != nil {
		logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	return value
}

func readConfigFilePathFlag(cmd *cobra.Command) string {
	value, err := cmd.Flags().GetString("telemetry-config-file-path")
	if err != nil {
		logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	return value
}

// DisallowUnknownFields is deliberate: a mistyped section name would otherwise be dropped
// silently, and a dropped section means disabled export.
func parseTelemetryConfig(cmd *cobra.Command) *ybav2client.TelemetryConfig {
	content := readInlineConfigFlag(cmd)
	if util.IsEmptyString(content) {
		filePath := readConfigFilePathFlag(cmd)
		fileBytes, err := os.ReadFile(filePath)
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		content = string(fileBytes)
	}

	config := ybav2client.TelemetryConfig{}
	decoder := json.NewDecoder(strings.NewReader(content))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(&config); err != nil {
		logrus.Fatal(formatter.Colorize(
			"Error unmarshaling telemetry config: "+err.Error()+"\n", formatter.RedColor))
	}
	return &config
}

// Reports what each section resolved to, so a run driven by provider names is auditable
// from the command output alone.
func logExporterResolution(
	config *ybav2client.TelemetryConfig,
	resolved map[string]ExporterInfo,
) {
	for _, section := range Sections(config) {
		if !section.IsExporting() {
			continue
		}
		names := make([]string, 0, len(section.ExporterUUIDs))
		for _, exporterUUID := range section.ExporterUUIDs {
			info := resolved[exporterUUID]
			names = append(names, fmt.Sprintf("%s (%s)", info.Name, exporterUUID))
		}
		logrus.Debugf("%s exports to %s\n", section.Name, strings.Join(names, ", "))
	}
}

func buildUpgradeOptions(cmd *cobra.Command) *ybav2client.ExportTelemetryUpgradeOptions {
	options := ybav2client.ExportTelemetryUpgradeOptions{}

	upgradeOption, err := cmd.Flags().GetString("upgrade-option")
	if err != nil {
		logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	switch strings.ToLower(upgradeOption) {
	case "rolling":
		options.SetRollingUpgrade(true)
	case "non-rolling":
		options.SetRollingUpgrade(false)
	default:
		logrus.Fatal(formatter.Colorize(
			"Invalid upgrade-option specified. Allowed values (case insensitive): "+
				"Rolling, Non-Rolling\n",
			formatter.RedColor))
	}

	masterDelay, err := cmd.Flags().GetInt32("delay-between-master-servers")
	if err != nil {
		logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	options.SetDelayBetweenMasterServers(masterDelay)

	tserverDelay, err := cmd.Flags().GetInt32("delay-between-tservers")
	if err != nil {
		logrus.Fatal(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	options.SetDelayBetweenTserverServers(tserverDelay)

	return &options
}

func init() {
	setTelemetryConfigCmd.Flags().SortFlags = false

	setTelemetryConfigCmd.Flags().String("telemetry-config", "",
		fmt.Sprintf(
			"[Optional] Telemetry export configuration to be set. Use the modified output "+
				"of \"yba universe upgrade export-telemetry-configs get\" as the flag value. "+
				"Quote the string with single quotes. %s",
			formatter.Colorize(
				"Provide either telemetry-config or telemetry-config-file-path",
				formatter.GreenColor)))
	setTelemetryConfigCmd.Flags().String("telemetry-config-file-path", "",
		fmt.Sprintf(
			"[Optional] Path to the modified json output file of "+
				"\"yba universe upgrade export-telemetry-configs get\". %s",
			formatter.Colorize(
				"Provide either telemetry-config or telemetry-config-file-path",
				formatter.GreenColor)))
	setTelemetryConfigCmd.MarkFlagsMutuallyExclusive(
		"telemetry-config", "telemetry-config-file-path")

	setTelemetryConfigCmd.Flags().String("upgrade-option", "Rolling",
		"[Optional] Upgrade option, defaults to Rolling. Allowed values (case "+
			"insensitive): Rolling, Non-Rolling (involves DB downtime).")
	setTelemetryConfigCmd.Flags().Int32("delay-between-master-servers", 18000,
		"[Optional] Upgrade delay between Master servers (in milliseconds).")
	setTelemetryConfigCmd.Flags().Int32("delay-between-tservers", 18000,
		"[Optional] Upgrade delay between Tservers (in milliseconds).")
	setTelemetryConfigCmd.Flags().Bool("dry-run", false,
		"[Optional] Only validate the input and report what would change.")
}
