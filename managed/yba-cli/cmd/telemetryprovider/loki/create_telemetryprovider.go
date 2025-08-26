/*
 * Copyright (c) YugaByte, Inc.
 */

package loki

import (
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/telemetryprovider/telemetryproviderutil"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// createLokiTelemetryProviderCmd represents the telemetryprovider command
var createLokiTelemetryProviderCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add"},
	Short:   "Create a YugabyteDB Anywhere Loki telemetry provider",
	Long:    "Create a Loki telemetry provider in YugabyteDB Anywhere",
	Example: `yba telemetryprovider loki create --name <name> \
     --endpoint <loki-endpoint> --auth-type <auth-type>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.CreateTelemetryProviderValidation(cmd)
		authType, err := cmd.Flags().GetString("auth-type")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if strings.EqualFold(authType, "basic") {
			username, err := cmd.Flags().GetString("username")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if util.IsEmptyString(username) {
				logrus.Fatalf(
					formatter.Colorize(
						"Username and password are required for basic authentication.\n",
						formatter.RedColor,
					),
				)
			}
		}
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		telemetryproviderutil.VersionCheck(authAPI)

		requestBody := util.TelemetryProvider{}
		config := util.TelemetryProviderConfig{
			Type: util.GetStringPointer(util.LokiTelemetryProviderType),
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
		if !util.IsEmptyString(endpoint) {
			config.SetEndpoint(endpoint)
		}

		tenantID, err := cmd.Flags().GetString("organization-id")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(tenantID) {
			config.SetOrganizationID(tenantID)
		}

		authType, err := cmd.Flags().GetString("auth-type")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}

		switch strings.ToLower(authType) {
		case "none":
			authType = util.NoLokiAuthType
		case "basic":
			authType = util.BasicLokiAuthType
			basicAuth := ybaclient.BasicAuthCredentials{}
			username, err := cmd.Flags().GetString("username")
			if err != nil {

				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if !util.IsEmptyString(username) {
				basicAuth.SetUsername(username)
			}
			password, err := cmd.Flags().GetString("password")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if !util.IsEmptyString(password) {
				basicAuth.SetPassword(password)
			}
			config.SetBasicAuth(basicAuth)
		default:
			logrus.Fatalf(formatter.Colorize("Invalid auth-type specified. "+
				"Allowed values: none, basic\n", formatter.RedColor))
		}
		config.SetAuthType(authType)

		requestBody.SetConfig(config)

		tags, err := cmd.Flags().GetStringToString("tags")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		requestBody.SetTags(tags)

		telemetryproviderutil.CreateTelemetryProviderUtil(
			authAPI, name, util.LokiTelemetryProviderType, requestBody)

	},
}

func init() {
	createLokiTelemetryProviderCmd.Flags().SortFlags = false

	createLokiTelemetryProviderCmd.Flags().String("endpoint", "", "[Required] Loki endpoint URL. "+
		"Provide the endpoint in the following format: http://<loki-url>:<loki-port>. "+
		"Ensure loki is accessible through the mentioned port."+
		" YugbayteDB Anywhere will push logs to <endpoint>/loki/api/v1/push.")

	createLokiTelemetryProviderCmd.MarkFlagRequired("endpoint")

	createLokiTelemetryProviderCmd.Flags().
		String("organization-id", "", "[Optional] Organization/Tenant ID is required when multi-tenancy is set up in Loki. "+
			"Optional for Grafana Cloud since the authentication reroutes requests according to scope.")

	createLokiTelemetryProviderCmd.Flags().
		String("auth-type", "none", "[Optional] Authentication type to be used for Loki telemetry provider. "+
			"Allowed values: none, basic.")

	createLokiTelemetryProviderCmd.Flags().
		String("username", "", "[Optional] Username for basic authentication. "+
			formatter.Colorize("Required if auth-type is set to basic.", formatter.GreenColor))
	createLokiTelemetryProviderCmd.Flags().
		String("password", "", "[Optional] Password for basic authentication. "+
			formatter.Colorize("Required if auth-type is set to basic.", formatter.GreenColor))

	createLokiTelemetryProviderCmd.MarkFlagsRequiredTogether("username", "password")

	createLokiTelemetryProviderCmd.Flags().StringToString("tags",
		map[string]string{}, "[Optional] Tags to be applied to the exporter config. Provide "+
			"as key-value pairs per flag. Example \"--tags "+
			"name=test --tags owner=development\" OR "+
			"\"--tags name=test,owner=development\".")

}
