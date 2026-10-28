/*
 * Copyright (c) YugabyteDB, Inc.
 */

package otlp

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

// createOTLPTelemetryProviderCmd represents the telemetryprovider command
var createOTLPTelemetryProviderCmd = &cobra.Command{
	Use:     "create",
	Aliases: []string{"add"},
	Short:   "Create a YugabyteDB Anywhere OTLP telemetry provider",
	Long: "Create an OTLP telemetry provider in YugabyteDB Anywhere. " +
		"Requires the global runtime configuration \"yb.telemetry.allow_otlp\" to be true.",
	Example: `yba telemetry-provider otlp create --name <name> \
     --endpoint <endpoint> --auth-type basic --username <username> --password <password>`,
	PreRun: func(cmd *cobra.Command, args []string) {
		telemetryproviderutil.CreateTelemetryProviderValidation(cmd)
	},
	Run: func(cmd *cobra.Command, args []string) {
		authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

		telemetryproviderutil.VersionCheck(authAPI)

		requestBody := util.TelemetryProvider{}
		config := util.TelemetryProviderConfig{
			Type: util.GetStringPointer(util.OTLPTelemetryProviderType),
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
			logrus.Fatalf(formatter.Colorize("OTLP Endpoint is required\n", formatter.RedColor))
		}
		config.SetEndpoint(endpoint)

		protocol, err := cmd.Flags().GetString("protocol")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		switch strings.ToLower(protocol) {
		case "grpc":
			protocol = util.GRPCOTLPProtocol
		case "http":
			protocol = util.HTTPOTLPProtocol
		default:
			logrus.Fatalf(formatter.Colorize("Invalid protocol specified. "+
				"Allowed values: gRPC, HTTP\n", formatter.RedColor))
		}
		config.SetProtocol(protocol)

		authType, err := cmd.Flags().GetString("auth-type")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		switch strings.ToLower(authType) {
		case "none":
			config.SetAuthType(util.NoAuthTelemetryAuthType)
		case "basic":
			username, err := cmd.Flags().GetString("username")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			password, err := cmd.Flags().GetString("password")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if util.IsEmptyString(username) || util.IsEmptyString(password) {
				logrus.Fatalf(
					formatter.Colorize(
						"Username and password are required for basic authentication.\n",
						formatter.RedColor,
					),
				)
			}
			basicAuth := ybaclient.BasicAuthCredentials{}
			basicAuth.SetUsername(username)
			basicAuth.SetPassword(password)
			config.SetBasicAuth(basicAuth)
			config.SetAuthType(util.BasicAuthTelemetryAuthType)
		case "bearertoken", "bearer-token", "bearer":
			token, err := cmd.Flags().GetString("bearer-token")
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
			if util.IsEmptyString(token) {
				logrus.Fatalf(
					formatter.Colorize(
						"Bearer token is required for bearer token authentication.\n",
						formatter.RedColor,
					),
				)
			}
			bearerToken := ybaclient.BearerToken{}
			bearerToken.SetToken(token)
			config.SetBearerToken(bearerToken)
			config.SetAuthType(util.BearerTokenTelemetryAuthType)
		default:
			logrus.Fatalf(formatter.Colorize("Invalid auth-type specified. "+
				"Allowed values: none, basic, bearer-token\n", formatter.RedColor))
		}

		compression, err := cmd.Flags().GetString("compression")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if !util.IsEmptyString(compression) {
			switch strings.ToLower(compression) {
			case util.GzipCompressionType,
				util.NoneCompressionType,
				util.SnappyCompressionType,
				util.ZstdCompressionType:
				config.SetCompression(strings.ToLower(compression))
			default:
				logrus.Fatalf(formatter.Colorize("Invalid compression specified. "+
					"Allowed values: gzip, none, snappy, zstd\n", formatter.RedColor))
			}
		}

		timeoutSeconds, err := cmd.Flags().GetInt32("timeout-seconds")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if timeoutSeconds <= 0 {
			logrus.Fatalf(
				formatter.Colorize("timeout-seconds must be positive\n", formatter.RedColor))
		}
		config.SetTimeoutSeconds(timeoutSeconds)

		// Per signal endpoint overrides only exist for otlphttp; gRPC is rejected.
		logsEndpoint, err := cmd.Flags().GetString("logs-endpoint")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		metricsEndpoint, err := cmd.Flags().GetString("metrics-endpoint")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if protocol != util.HTTPOTLPProtocol &&
			(!util.IsEmptyString(logsEndpoint) || !util.IsEmptyString(metricsEndpoint)) {
			logrus.Fatalf(
				formatter.Colorize(
					"logs-endpoint and metrics-endpoint are allowed only for the HTTP protocol\n",
					formatter.RedColor,
				),
			)
		}
		if !util.IsEmptyString(logsEndpoint) {
			config.SetLogsEndpoint(logsEndpoint)
		}
		if !util.IsEmptyString(metricsEndpoint) {
			config.SetMetricsEndpoint(metricsEndpoint)
		}

		headers, err := cmd.Flags().GetStringToString("headers")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		if len(headers) > 0 {
			config.SetHeaders(headers)
		}

		retryConfig := buildRetryConfig(cmd)
		if retryConfig != nil {
			config.SetRetryOnFailure(*retryConfig)
		}

		requestBody.SetConfig(config)

		tags, err := cmd.Flags().GetStringToString("tags")
		if err != nil {
			logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
		}
		requestBody.SetTags(tags)

		telemetryproviderutil.CreateTelemetryProviderUtil(
			authAPI, name, util.OTLPTelemetryProviderType, requestBody)

	},
}

// Returns nil when no retry flag was set, leaving the collector defaults in place.
func buildRetryConfig(cmd *cobra.Command) *ybaclient.ExporterRetryConfig {
	retryFlags := []string{
		"retry-enabled",
		"retry-initial-interval",
		"retry-max-interval",
		"retry-max-elapsed-time",
	}
	changed := false
	for _, flag := range retryFlags {
		if cmd.Flags().Changed(flag) {
			changed = true
			break
		}
	}
	if !changed {
		return nil
	}

	retryConfig := ybaclient.ExporterRetryConfig{}

	enabled, err := cmd.Flags().GetBool("retry-enabled")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	retryConfig.SetEnabled(enabled)

	initialInterval, err := cmd.Flags().GetString("retry-initial-interval")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if !util.IsEmptyString(initialInterval) {
		retryConfig.SetInitialInterval(initialInterval)
	}

	maxInterval, err := cmd.Flags().GetString("retry-max-interval")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if !util.IsEmptyString(maxInterval) {
		retryConfig.SetMaxInterval(maxInterval)
	}

	maxElapsedTime, err := cmd.Flags().GetString("retry-max-elapsed-time")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	if !util.IsEmptyString(maxElapsedTime) {
		retryConfig.SetMaxElapsedTime(maxElapsedTime)
	}

	return &retryConfig
}

func init() {
	createOTLPTelemetryProviderCmd.Flags().SortFlags = false

	createOTLPTelemetryProviderCmd.Flags().String("endpoint", "",
		"[Required] OTLP collector endpoint. For HTTP protocol log export, "+
			"\"/v1/logs\" is appended.")
	createOTLPTelemetryProviderCmd.MarkFlagRequired("endpoint")
	createOTLPTelemetryProviderCmd.Flags().String("protocol", "gRPC",
		"[Optional] OTLP protocol, defaults to gRPC. Allowed values (case insensitive): "+
			"gRPC, HTTP.")
	createOTLPTelemetryProviderCmd.Flags().String("auth-type", "none",
		"[Optional] OTLP authentication type, defaults to none. "+
			"Allowed values (case insensitive): none, basic, bearer-token.")
	createOTLPTelemetryProviderCmd.Flags().String("username", "",
		formatter.Colorize(
			"[Optional] Username. Required with password for basic authentication.",
			formatter.GreenColor))
	createOTLPTelemetryProviderCmd.Flags().String("password", "",
		formatter.Colorize(
			"[Optional] Password. Required with username for basic authentication.",
			formatter.GreenColor))
	createOTLPTelemetryProviderCmd.MarkFlagsRequiredTogether("username", "password")
	createOTLPTelemetryProviderCmd.Flags().String("bearer-token", "",
		formatter.Colorize(
			"[Optional] Bearer token. Required for bearer-token authentication.",
			formatter.GreenColor))
	createOTLPTelemetryProviderCmd.Flags().String("compression", "",
		"[Optional] Compression for exported data, defaults to gzip. "+
			"Allowed values (case insensitive): gzip, none, snappy, zstd.")
	createOTLPTelemetryProviderCmd.Flags().Int32("timeout-seconds", 5,
		"[Optional] Export request timeout in seconds.")
	createOTLPTelemetryProviderCmd.Flags().String("logs-endpoint", "",
		"[Optional] Target URL for logs, overriding endpoint. HTTP protocol only.")
	createOTLPTelemetryProviderCmd.Flags().String("metrics-endpoint", "",
		"[Optional] Target URL for metrics, overriding endpoint. HTTP protocol only.")
	createOTLPTelemetryProviderCmd.Flags().StringToString("headers",
		map[string]string{}, "[Optional] Headers to send with each export request. Provide "+
			"as key-value pairs per flag. Example \"--headers "+
			"X-Scope-OrgID=tenant1 --headers X-Custom=value\".")
	createOTLPTelemetryProviderCmd.Flags().Bool("retry-enabled", false,
		"[Optional] Enable exporter retry on failure.")
	createOTLPTelemetryProviderCmd.Flags().String("retry-initial-interval", "",
		"[Optional] Initial retry interval as a duration, for example \"5s\", \"1m\".")
	createOTLPTelemetryProviderCmd.Flags().String("retry-max-interval", "",
		"[Optional] Maximum retry interval as a duration, for example \"30s\", \"1m\".")
	createOTLPTelemetryProviderCmd.Flags().String("retry-max-elapsed-time", "",
		"[Optional] Maximum total retry time as a duration, for example \"5m\", \"60m\".")
	createOTLPTelemetryProviderCmd.Flags().StringToString("tags",
		map[string]string{}, "[Optional] Tags to be applied to the exporter config. Provide "+
			"as key-value pairs per flag. Example \"--tags "+
			"name=test --tags owner=development\" OR "+
			"\"--tags name=test,owner=development\".")
}
