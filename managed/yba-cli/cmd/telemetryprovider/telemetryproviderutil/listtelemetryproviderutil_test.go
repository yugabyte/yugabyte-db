/*
 * Copyright (c) YugabyteDB, Inc.
 */

package telemetryproviderutil

import (
	"testing"

	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

func TestResolveTelemetryProviderType(t *testing.T) {
	tests := []struct {
		in   string
		want string
	}{
		// Command names, aliases and wire values all resolve, in any case.
		{"datadog", util.DataDogTelemetryProviderType},
		{"DataDog", util.DataDogTelemetryProviderType},
		{"dd", util.DataDogTelemetryProviderType},
		{"DATA_DOG", util.DataDogTelemetryProviderType},
		{"awscloudwatch", util.AWSCloudWatchTelemetryProviderType},
		{"AWS", util.AWSCloudWatchTelemetryProviderType},
		{"cloudwatch", util.AWSCloudWatchTelemetryProviderType},
		{"gcp", util.GCPCloudMonitoringTelemetryProviderType},
		{"GCP_CLOUD_MONITORING", util.GCPCloudMonitoringTelemetryProviderType},
		{"Loki", util.LokiTelemetryProviderType},
		{"SPLUNK", util.SplunkTelemetryProviderType},
		{"dynatrace", util.DynatraceTelemetryProviderType},
		{"DT", util.DynatraceTelemetryProviderType},
		{"S3", util.S3TelemetryProviderType},
		{"awss3", util.S3TelemetryProviderType},
		{"otlp", util.OTLPTelemetryProviderType},
		{"OpenTelemetry", util.OTLPTelemetryProviderType},
		// An empty filter means "no filtering", so it must survive unchanged.
		{"", ""},
		// An unknown value is returned as-is so filtering matches nothing rather than
		// silently listing every provider.
		{"nosuchtype", "nosuchtype"},
	}
	for _, tc := range tests {
		if got := ResolveTelemetryProviderType(tc.in); got != tc.want {
			t.Errorf("ResolveTelemetryProviderType(%q) = %q, want %q", tc.in, got, tc.want)
		}
	}
}
