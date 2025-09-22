/*
 * Copyright (c) YugaByte, Inc.
 */

package datadog

import (
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/telemetryproviderutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// createDataDogTelemetryProviderCmd represents the telemetryprovider command
var createDataDogTelemetryProviderCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add"},
	Short:   "Create a YugabyteDB Anywhere DataDog telemetry provider",
	Long:    "Create a DataDog telemetry provider in YugabyteDB Anywhere",
	Example: `yba telemetryprovider datadog create --name <name> \
    --api-key <api-key> --site <site>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.CreateTelemetryProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		telemetryproviderutil.VersionCheck(authAPI)

		requestBody := util.TelemetryProvider{}
		config := util.TelemetryProviderConfig{
			Type: util.GetStringPointer(util.DataDogTelemetryProviderType),
		}

		name, err := cmd.Flags().GetString("name")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		requestBody.SetName(name)

		apiKey, err := cmd.Flags().GetString("api-key")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(apiKey) == 0 {
			logrus.Fatalf(
				formatter.Colorize(
					"No API key specified to create DataDog telemetry provider\n",
					formatter.RedColor))
		}
		config.SetApiKey(apiKey)

		site, err := cmd.Flags().GetString("site")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(site) {
			config.SetSite(site)
		}
		requestBody.SetConfig(config)

		tags, err := cmd.Flags().GetStringToString("tags")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		requestBody.SetTags(tags)

		telemetryproviderutil.CreateTelemetryProviderUtil(
			authAPI, name, util.DataDogTelemetryProviderType, requestBody)

	},
}

func init() {
	createDataDogTelemetryProviderCmd.Flags().SortFlags = false

	createDataDogTelemetryProviderCmd.Flags().String("api-key", "",
		"[Required] DataDog API Key.")
	createDataDogTelemetryProviderCmd.MarkFlagRequired("api-key")
	createDataDogTelemetryProviderCmd.Flags().String("site", "datadoghq.com",
		"[Optional] DataDog Site URL.")
	createDataDogTelemetryProviderCmd.Flags().StringToString("tags",
		map[string]string{}, "[Optional] Tags to be applied to the exporter config. Provide "+
			"as key-value pairs per flag. Example \"--tags "+
			"name=test --tags owner=development\" OR "+
			"\"--tags name=test,owner=development\".")

}
