/*
 * Copyright (c) YugabyteDB, Inc.
 */

package telemetryconfig

import (
	"encoding/json"
	"os"
	"strings"
	"testing"

	ybav2client "github.com/yugabyte/platform-go-client/v2"
)

const templatePath = "../../../../templates/upgrade-universe-telemetry-config-input.json"

// set decodes with DisallowUnknownFields, which makes the shipped template a contract:
// every key in it has to exist on the API model.
func TestTemplateDecodesStrictly(t *testing.T) {
	content, err := os.ReadFile(templatePath)
	if err != nil {
		t.Fatalf("read template: %v", err)
	}
	config := ybav2client.TelemetryConfig{}
	decoder := json.NewDecoder(strings.NewReader(string(content)))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(&config); err != nil {
		t.Fatalf("template does not decode against the API model: %v", err)
	}

	sections := Sections(&config)
	if len(sections) != len(sectionDefs) {
		t.Fatalf("got %d sections, want %d", len(sections), len(sectionDefs))
	}
	for _, section := range sections {
		if !section.Present {
			t.Errorf("template is missing section %s", section.Name)
		}
		if !section.IsExporting() {
			t.Errorf("template section %s has no exporter", section.Name)
		}
	}
}

func TestSectionsTreatsAbsentAsDisabled(t *testing.T) {
	config := ybav2client.TelemetryConfig{
		AuditLogs: &ybav2client.AuditLogsTelemetrySpec{
			Exporters: []ybav2client.UniverseLogsExporterConfig{{ExporterUuid: "a"}},
		},
		// A present section with an empty exporter list is explicitly off, which is a
		// different state from being absent.
		Metrics: &ybav2client.MetricsTelemetrySpec{},
	}
	byName := map[string]Section{}
	for _, section := range Sections(&config) {
		byName[section.Name] = section
	}

	if !byName["audit_logs"].IsExporting() {
		t.Error("audit_logs should be exporting")
	}
	if byName["metrics"].Present != true {
		t.Error("metrics should be present")
	}
	if byName["metrics"].IsExporting() {
		t.Error("metrics has no exporters and should not be exporting")
	}
	if byName["query_logs"].Present {
		t.Error("query_logs is absent and should not be present")
	}
	if byName["metrics"].Signal != signalMetrics {
		t.Errorf("metrics signal = %q, want %q", byName["metrics"].Signal, signalMetrics)
	}
	if !byName["node_agent_logs"].ServerLog {
		t.Error("node_agent_logs should be flagged as a server log section")
	}
	if byName["node_agent_logs"].SupportedOnKubernetes {
		t.Error("node_agent_logs is not supported on Kubernetes")
	}
}

func TestDisabledSections(t *testing.T) {
	exporting := func() *ybav2client.TelemetryConfig {
		return &ybav2client.TelemetryConfig{
			AuditLogs: &ybav2client.AuditLogsTelemetrySpec{
				Exporters: []ybav2client.UniverseLogsExporterConfig{{ExporterUuid: "a"}},
			},
			Metrics: &ybav2client.MetricsTelemetrySpec{
				Exporters: []ybav2client.UniverseMetricsExporterConfig{{ExporterUuid: "a"}},
			},
		}
	}

	// Omitting a section is what disables it.
	current := exporting()
	requested := &ybav2client.TelemetryConfig{
		AuditLogs: &ybav2client.AuditLogsTelemetrySpec{
			Exporters: []ybav2client.UniverseLogsExporterConfig{{ExporterUuid: "a"}},
		},
	}
	disabled := DisabledSections(current, requested)
	if len(disabled) != 1 || disabled[0] != "metrics" {
		t.Errorf("DisabledSections = %v, want [metrics]", disabled)
	}

	disabled = DisabledSections(exporting(), &ybav2client.TelemetryConfig{})
	if len(disabled) != 2 {
		t.Errorf("DisabledSections = %v, want audit_logs and metrics", disabled)
	}

	disabled = DisabledSections(&ybav2client.TelemetryConfig{}, exporting())
	if len(disabled) != 0 {
		t.Errorf("DisabledSections = %v, want none", disabled)
	}
}

func TestIsSameConfig(t *testing.T) {
	a := &ybav2client.TelemetryConfig{
		AuditLogs: &ybav2client.AuditLogsTelemetrySpec{
			Exporters: []ybav2client.UniverseLogsExporterConfig{{ExporterUuid: "u1"}},
		},
	}
	b := &ybav2client.TelemetryConfig{
		AuditLogs: &ybav2client.AuditLogsTelemetrySpec{
			Exporters: []ybav2client.UniverseLogsExporterConfig{{ExporterUuid: "u1"}},
		},
	}
	if !IsSameConfig(a, b) {
		t.Error("identical configs should compare equal")
	}
	b.AuditLogs.Exporters[0].ExporterUuid = "u2"
	if IsSameConfig(a, b) {
		t.Error("configs with different exporters should not compare equal")
	}
	if IsSameConfig(a, &ybav2client.TelemetryConfig{}) {
		t.Error("a configured document should not equal an empty one")
	}
}

func TestStaticSinkCapabilitiesCoverEveryProviderType(t *testing.T) {
	// A provider type the CLI can create but the fallback table cannot judge would skip
	// validation silently.
	for _, providerType := range []string{
		"DATA_DOG", "SPLUNK", "AWS_CLOUDWATCH", "GCP_CLOUD_MONITORING",
		"LOKI", "DYNATRACE", "S3", "OTLP",
	} {
		if _, ok := staticSinkCapabilities[providerType]; !ok {
			t.Errorf("staticSinkCapabilities is missing %s", providerType)
		}
	}
	if staticSinkCapabilities["DYNATRACE"].allowedForLogs {
		t.Error("DYNATRACE is a metrics-only sink")
	}
	if staticSinkCapabilities["S3"].allowedForMetrics {
		t.Error("S3 is a logs-only sink")
	}
}
