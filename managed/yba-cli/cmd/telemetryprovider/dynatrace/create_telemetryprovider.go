/*
 * Copyright (c) YugabyteDB, Inc.
 */

package dynatrace

import (
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/telemetryproviderutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// createDynatraceTelemetryProviderCmd represents the telemetryprovider command
var createDynatraceTelemetryProviderCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add"},
	Short:   "Create a YugabyteDB Anywhere Dynatrace telemetry provider",
	Long:    "Create a Dynatrace telemetry provider in YugabyteDB Anywhere",
	Example: `yba telemetry-provider dynatrace create --name <name> \
     --endpoint <endpoint> --api-token <api-token>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.CreateTelemetryProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		telemetryproviderutil.VersionCheck(authAPI)

		requestBody := util.TelemetryProvider{}
		config := util.TelemetryProviderConfig{
			Type: util.GetStringPointer(util.DynatraceTelemetryProviderType),
		}

		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		requestBody.SetName(name)

		endpoint, err := cmd.Flags().GetString("endpoint")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(endpoint) {
			logrus.Fatalf(
				formatter.Colorize("Dynatrace Endpoint is required\n", formatter.RedColor),
			)
		}
		config.SetEndpoint(endpoint)

		apiToken, err := cmd.Flags().GetString("api-token")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(apiToken) {
			logrus.Fatalf(
				formatter.Colorize("Dynatrace API Token is required\n", formatter.RedColor))
		}
		config.SetApiToken(apiToken)

		requestBody.SetConfig(config)

		tags, err := cmd.Flags().GetStringToString("tags")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		requestBody.SetTags(tags)

		telemetryproviderutil.CreateTelemetryProviderUtil(
			authAPI, name, util.DynatraceTelemetryProviderType, requestBody)

	},
}

func init() {
	createDynatraceTelemetryProviderCmd.Flags().SortFlags = false

	createDynatraceTelemetryProviderCmd.Flags().String("endpoint", "",
		"[Required] Dynatrace environment endpoint, for example "+
			"https://<environment-id>.live.dynatrace.com.")
	createDynatraceTelemetryProviderCmd.MarkFlagRequired("endpoint")
	createDynatraceTelemetryProviderCmd.Flags().String("api-token", "",
		"[Required] Dynatrace API token. Requires the metrics.ingest, logs.ingest and "+
			"openTelemetryTrace.ingest scopes.")
	createDynatraceTelemetryProviderCmd.MarkFlagRequired("api-token")
	createDynatraceTelemetryProviderCmd.Flags().StringToString("tags",
		map[string]string{}, "[Optional] Tags to be applied to the exporter config. Provide "+
			"as key-value pairs per flag. Example \"--tags "+
			"name=test --tags owner=development\" OR "+
			"\"--tags name=test,owner=development\".")
}
