/*
 * Copyright (c) YugabyteDB, Inc.
 */

package endpoint

import (
	"testing"

	"github.com/spf13/cobra"
	ybav2client "github.com/yugabyte/platform-go-client/v2"
)

// The endpoint API replaces the whole record, so an update that omits a flag
// must carry the stored value forward rather than blanking the field. That is
// the whole reason endpointSpecFromFlags takes the current spec.
func TestEndpointSpecFromFlagsKeepsUnspecifiedFieldsOnUpdate(t *testing.T) {
	cmd := &cobra.Command{}
	addEndpointFlags(cmd)
	if err := cmd.Flags().Set("name", "byoc-prod"); err != nil {
		t.Fatalf("setting name: %v", err)
	}
	if err := cmd.Flags().Set("password", "rotated"); err != nil {
		t.Fatalf("setting password: %v", err)
	}
	if err := cmd.Flags().Set("auth-type", "BASIC"); err != nil {
		t.Fatalf("setting auth-type: %v", err)
	}
	if err := cmd.Flags().Set("username", "writer"); err != nil {
		t.Fatalf("setting username: %v", err)
	}

	current := ybav2client.PerfAdvisorEndpointSpec{
		Name:               "byoc-prod",
		Type:               ybav2client.PerfAdvisorEndpointType("BYOC"),
		CollectionEndpoint: "https://byoc.cloud.yugabyte.com",
		MetricsEndpoint:    "https://byoc.cloud.yugabyte.com/api/v1/otlp/metrics",
		MetricsType:        ybav2client.PerfAdvisorEndpointMetricsType("otlphttp"),
		YbmAccountId:       stringPtr("account-1"),
		YbmProjectId:       stringPtr("project-1"),
	}

	spec := endpointSpecFromFlags(cmd, &current)

	if spec.CollectionEndpoint != current.CollectionEndpoint {
		t.Errorf("collection endpoint was not carried over: %q", spec.CollectionEndpoint)
	}
	if spec.MetricsEndpoint != current.MetricsEndpoint {
		t.Errorf("metrics endpoint was not carried over: %q", spec.MetricsEndpoint)
	}
	if spec.GetYbmAccountId() != "account-1" || spec.GetYbmProjectId() != "project-1" {
		t.Errorf("YBM identifiers were not carried over: %q / %q",
			spec.GetYbmAccountId(), spec.GetYbmProjectId())
	}
	// The rotated credential does reach both endpoints.
	if spec.CollectionAuth == nil || spec.CollectionAuth.GetPassword() != "rotated" {
		t.Errorf("collection password was not updated: %+v", spec.CollectionAuth)
	}
	if spec.MetricsAuth == nil || spec.MetricsAuth.GetPassword() != "rotated" {
		t.Errorf("metrics password was not updated: %+v", spec.MetricsAuth)
	}
}

// On create there is nothing to carry forward, so the defaults apply.
func TestEndpointSpecFromFlagsAppliesDefaultsOnCreate(t *testing.T) {
	cmd := &cobra.Command{}
	addEndpointFlags(cmd)
	for flag, value := range map[string]string{
		"name":                "standalone",
		"collection-endpoint": "https://pa.example.com:9443",
		"metrics-endpoint":    "https://pa.example.com:9443/api/v1/otlp/metrics",
	} {
		if err := cmd.Flags().Set(flag, value); err != nil {
			t.Fatalf("setting %s: %v", flag, err)
		}
	}

	spec := endpointSpecFromFlags(cmd, nil)

	if string(spec.Type) != "BYOC" {
		t.Errorf("expected the BYOC default, got %q", spec.Type)
	}
	if string(spec.MetricsType) != "otlphttp" {
		t.Errorf("expected the otlphttp default, got %q", spec.MetricsType)
	}
	// No auth flags passed, so nothing is sent and YBA keeps its own default.
	if spec.CollectionAuth != nil || spec.MetricsAuth != nil {
		t.Errorf("expected no auth to be sent, got %+v / %+v",
			spec.CollectionAuth, spec.MetricsAuth)
	}
}

func stringPtr(in string) *string {
	return &in
}

// The API spells these enums in fixed case - BYOC and NONE/BASIC upper, otlphttp and
// remotewrite lower - and the rest of the CLI normalises such flags (telemetryprovider
// does it with strings.ToLower/ToUpper), so an operator typing them in any case works.
func TestEndpointSpecFromFlagsNormalisesEnumCase(t *testing.T) {
	cmd := &cobra.Command{Use: "test"}
	addEndpointFlags(cmd)
	if err := cmd.Flags().Parse([]string{
		"--name", "e",
		"--collection-endpoint", "https://host",
		"--metrics-endpoint", "https://host/api/v1/otlp/metrics",
		"--type", "byoc",
		"--metrics-type", "OTLPHTTP",
		"--auth-type", "basic",
		"--username", "u",
		"--password", "p",
	}); err != nil {
		t.Fatalf("parsing flags: %v", err)
	}

	spec := endpointSpecFromFlags(cmd, nil)

	if got := string(spec.Type); got != "BYOC" {
		t.Errorf("type: got %q, want BYOC", got)
	}
	if got := string(spec.MetricsType); got != "otlphttp" {
		t.Errorf("metrics-type: got %q, want otlphttp", got)
	}
	// One credential pair covers both endpoints, so both carry the normalised type.
	if spec.CollectionAuth == nil || spec.CollectionAuth.Type != "BASIC" {
		t.Errorf("collection auth-type: got %+v, want BASIC", spec.CollectionAuth)
	}
	if spec.MetricsAuth == nil || spec.MetricsAuth.Type != "BASIC" {
		t.Errorf("metrics auth-type: got %+v, want BASIC", spec.MetricsAuth)
	}
}
