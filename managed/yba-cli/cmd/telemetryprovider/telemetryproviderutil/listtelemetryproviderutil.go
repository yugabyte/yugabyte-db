/*
 * Copyright (c) YugabyteDB, Inc.
 */

package telemetryproviderutil

import (
	"fmt"
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/telemetryprovider"
)

// ListTelemetryProviderUtil executes the list telemetry provider command
func ListTelemetryProviderUtil(
	cmd *cobra.Command,
	commandCall string,
	telemetryProviderType string,
) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()

	VersionCheck(authAPI)

	callSite := "Telemetry Provider"
	if !util.IsEmptyString(commandCall) {
		callSite = fmt.Sprintf("%s: %s", callSite, commandCall)
	}

	// filter by name
	telemetryProviderName, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}

	r := ListAndFilterTelemetryProviders(
		authAPI, callSite, "List", telemetryProviderName, telemetryProviderType)

	telemetryProviderCtx := formatter.Context{
		Command: "list",
		Output:  os.Stdout,
		Format:  telemetryprovider.NewTelemetryProviderFormat(viper.GetString("output")),
	}
	if len(r) < 1 {
		if util.IsOutputType(formatter.TableFormatKey) {
			logrus.Info("No telemetry providers found\n")
		} else {
			logrus.Info("[]\n")
		}
		return
	}
	telemetryprovider.Write(telemetryProviderCtx, r)
}

// ListAndFilterTelemetryProviders lists and filters telemetry providers based on
// the provided criteria
func ListAndFilterTelemetryProviders(
	authAPI *ybaAuthClient.AuthAPIClient,
	callSite string,
	operation string,
	name string,
	providerType string,
) []util.TelemetryProvider {
	r, err := authAPI.ListTelemetryProvidersRest(callSite, operation)
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}
	rName := make([]util.TelemetryProvider, 0)
	if !util.IsEmptyString(name) {
		for _, v := range r {
			if strings.Compare(v.GetName(), name) == 0 {
				rName = append(rName, v)
			}
		}
		r = rName
	}

	providerType = ResolveTelemetryProviderType(providerType)

	rType := make([]util.TelemetryProvider, 0)
	if !util.IsEmptyString(providerType) {
		for _, v := range r {
			config := v.GetConfig()
			if strings.Compare(config.GetType(), providerType) == 0 {
				rType = append(rType, v)
			}
		}
		r = rType
	}
	return r
}

// ResolveTelemetryProviderType maps a command alias ("gcp") or wire value
// ("GCP_CLOUD_MONITORING") to the wire value, case insensitively. An unrecognized value is
// returned unchanged, so filtering matches nothing rather than listing everything.
func ResolveTelemetryProviderType(providerType string) string {
	switch strings.ToLower(providerType) {
	case "datadog", "dd", "data_dog":
		return util.DataDogTelemetryProviderType
	case "loki", "lo":
		return util.LokiTelemetryProviderType
	case "splunk", "sp":
		return util.SplunkTelemetryProviderType
	case "awscloudwatch", "aws", "cloudwatch", "aws_cloudwatch":
		return util.AWSCloudWatchTelemetryProviderType
	case "gcpcloudmonitoring", "gcp", "gcp_cloud_monitoring":
		return util.GCPCloudMonitoringTelemetryProviderType
	case "dynatrace", "dt":
		return util.DynatraceTelemetryProviderType
	case "s3", "awss3":
		return util.S3TelemetryProviderType
	case "otlp", "opentelemetry":
		return util.OTLPTelemetryProviderType
	}
	return providerType
}
