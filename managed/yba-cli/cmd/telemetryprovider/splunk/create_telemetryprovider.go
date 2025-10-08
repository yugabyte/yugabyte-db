/*
 * Copyright (c) YugabyteDB, Inc.
 */

package splunk

import (
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/telemetryproviderutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// createSplunkTelemetryProviderCmd represents the telemetryprovider command
var createSplunkTelemetryProviderCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add"},
	Short:   "Create a YugabyteDB Anywhere Splunk telemetry provider",
	Long:    "Create a Splunk telemetry provider in YugabyteDB Anywhere",
	Example: `yba telemetryprovider splunk create --name <name> \
     --endpoint <endpoint> --access-token <access-token>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.CreateTelemetryProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		telemetryproviderutil.VersionCheck(authAPI)

		requestBody := util.TelemetryProvider{}
		config := util.TelemetryProviderConfig{
			Type: util.GetStringPointer(util.SplunkTelemetryProviderType),
		}

		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		requestBody.SetName(name)

		accessToken, err := cmd.Flags().GetString("access-token")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(accessToken) {
			logrus.Fatalf(
				formatter.Colorize("Splunk Access Token is required\n", formatter.RedColor),
			)
		}
		config.SetToken(accessToken)

		endpoint, err := cmd.Flags().GetString("endpoint")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if util.IsEmptyString(endpoint) {
			logrus.Fatalf(formatter.Colorize("Splunk Endpoint is required\n", formatter.RedColor))
		}
		config.SetEndpoint(endpoint)

		source, err := cmd.Flags().GetString("source")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(source) {
			config.SetSource(source)
		}

		sourceType, err := cmd.Flags().GetString("source-type")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(sourceType) {
			config.SetSourceType(sourceType)
		}

		index, err := cmd.Flags().GetString("index")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(index) {
			config.SetIndex(index)
		}

		requestBody.SetConfig(config)

		tags, err := cmd.Flags().GetStringToString("tags")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		requestBody.SetTags(tags)

		telemetryproviderutil.CreateTelemetryProviderUtil(
			authAPI, name, util.SplunkTelemetryProviderType, requestBody)

	},
}

func init() {
	createSplunkTelemetryProviderCmd.Flags().SortFlags = false

	createSplunkTelemetryProviderCmd.Flags().
		String("access-token", "", "[Required] Splunk Access Token.")
	createSplunkTelemetryProviderCmd.MarkFlagRequired("access-token")
	createSplunkTelemetryProviderCmd.Flags().String("endpoint", "", "[Required] Splunk Endpoint.")
	createSplunkTelemetryProviderCmd.MarkFlagRequired("endpoint")
	createSplunkTelemetryProviderCmd.Flags().String("source", "", "[Optional] Splunk Source.")
	createSplunkTelemetryProviderCmd.Flags().
		String("source-type", "", "[Optional] Splunk Source Type.")
	createSplunkTelemetryProviderCmd.Flags().String("index", "", "[Optional] Splunk Index.")
	createSplunkTelemetryProviderCmd.Flags().StringToString("tags",
		map[string]string{}, "[Optional] Tags to be applied to the exporter config. Provide "+
			"as key-value pairs per flag. Example \"--tags "+
			"name=test --tags owner=development\" OR "+
			"\"--tags name=test,owner=development\".")

}
