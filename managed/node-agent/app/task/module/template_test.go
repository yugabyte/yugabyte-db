// Copyright (c) YugabyteDB, Inc.

package module

import (
	"context"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func writeTestTemplate(t *testing.T, content string) string {
	testFile, err := os.CreateTemp("/tmp", "*test.j2")
	if err != nil {
		t.Fatalf("Failed to create temp directory: %v", err)
	}
	defer testFile.Close()
	err = os.WriteFile(testFile.Name(), []byte(content), 0644)
	if err != nil {
		t.Fatalf("Failed to write to temp file: %v", err)
	}
	return testFile.Name()
}

func TestServerTemplate(t *testing.T) {
	gflags := map[string]string{
		"key1": "value1",
		"key2": "value2",
		"key3": "value3",
	}
	gflagsContext := map[string]any{
		"gflags": gflags,
	}
	projectDir := os.Getenv("PROJECT_DIR")
	templatePath := filepath.Join(projectDir, "resources/templates/server/yb-server-gflags.j2")
	output, err := ResolveTemplate(
		context.TODO(),
		gflagsContext,
		templatePath,
	)
	if err != nil {
		t.Fatalf("Failed to copy file: %v", err)
	}
	t.Logf("Output: %s", output)
}

func TestCleanCoresTemplate(t *testing.T) {
	projectDir := os.Getenv("PROJECT_DIR")
	if projectDir == "" {
		t.Fatal("PROJECT_DIR is not set")
	}
	templatePath := filepath.Join(projectDir, "resources/templates/server/clean_cores.sh.j2")

	tests := []struct {
		name          string
		values        map[string]any
		wantLine      string
		wantErrSubstr string
	}{
		{
			name: "uses provided num_cores_to_keep",
			values: map[string]any{
				"num_cores_to_keep": 10,
				"yb_home_dir":       "/home/yugabyte",
				"yb_cores_dir":      "/home/yugabyte/cores",
			},
			wantLine: "num_cores_to_keep=10",
		},
		{
			name: "defaults num_cores_to_keep to 5",
			values: map[string]any{
				"yb_home_dir":  "/home/yugabyte",
				"yb_cores_dir": "/home/yugabyte/cores",
			},
			wantLine: "num_cores_to_keep=5",
		},
		{
			name: "fails when required vars are missing",
			values: map[string]any{
				"num_cores_to_keep": 5,
			},
			wantErrSubstr: "yb_home_dir",
		},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			output, err := ResolveTemplateStrict(
				context.TODO(),
				tc.values,
				templatePath,
				true, /*strictUndefined*/
			)
			if tc.wantErrSubstr != "" {
				if err == nil {
					t.Fatalf(
						"expected error containing %q, got output:\n%s",
						tc.wantErrSubstr,
						output,
					)
				}
				if !strings.Contains(err.Error(), tc.wantErrSubstr) {
					t.Fatalf("expected error containing %q, got: %v", tc.wantErrSubstr, err)
				}
				return
			}
			if err != nil {
				t.Fatalf("ResolveTemplateStrict failed: %v", err)
			}
			if !strings.Contains(output, tc.wantLine) {
				t.Fatalf("Expected %q in output:\n%s", tc.wantLine, output)
			}
			if !strings.Contains(
				output,
				`if [[ -z "${num_cores_to_keep}" || ! "${num_cores_to_keep}" =~ ^[0-9]+$ ]]; then`,
			) {
				t.Fatalf("Expected num_cores_to_keep validation in output:\n%s", output)
			}
			if strings.Contains(output, "yb_num_clean_cores_to_keep") {
				t.Fatalf("Output still references obsolete template variable")
			}
		})
	}
}

func TestClockSyncTemplate(t *testing.T) {
	projectDir := os.Getenv("PROJECT_DIR")
	if projectDir == "" {
		t.Fatal("PROJECT_DIR is not set")
	}
	templatePath := filepath.Join(projectDir, "resources/templates/server/clock-sync.sh.j2")

	tests := []struct {
		name     string
		values   map[string]any
		wantLine string
	}{
		{
			name: "uses provided clock skew knobs",
			values: map[string]any{
				"is_acceptable_clock_skew_wait_enabled": false,
				"acceptable_clock_skew_sec":             1.5,
				"acceptable_clock_skew_max_tries":       60,
				"mount_paths":                           "/mnt/d0",
			},
			wantLine: `is_acceptable_clock_skew_wait_enabled="False"`,
		},
		{
			name: "defaults when knobs are unset",
			values: map[string]any{
				"mount_paths": "/mnt/d0",
			},
			wantLine: `is_acceptable_clock_skew_wait_enabled="True"`,
		},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			output, err := ResolveTemplate(
				context.TODO(),
				tc.values,
				templatePath,
			)
			if err != nil {
				t.Fatalf("ResolveTemplate failed: %v", err)
			}
			if !strings.Contains(output, tc.wantLine) {
				t.Fatalf("Expected %q in output:\n%s", tc.wantLine, output)
			}
			if tc.name == "uses provided clock skew knobs" {
				if !strings.Contains(output, `acceptable_clock_skew_sec="1.5"`) {
					t.Fatalf("Expected acceptable_clock_skew_sec=1.5 in output:\n%s", output)
				}
				if !strings.Contains(output, `max_tries="60"`) {
					t.Fatalf("Expected max_tries=60 in output:\n%s", output)
				}
			}
		})
	}
}

func TestCollectMetricsWrapperTemplate(t *testing.T) {
	projectDir := os.Getenv("PROJECT_DIR")
	if projectDir == "" {
		t.Fatal("PROJECT_DIR is not set")
	}
	templatePath := filepath.Join(
		projectDir,
		"resources/templates/server/collect_metrics_wrapper.sh.j2",
	)

	tests := []struct {
		name     string
		values   map[string]any
		wantLine string
	}{
		{
			name: "uses provided yb_metrics_dir",
			values: map[string]any{
				"yb_home_dir":    "/home/yugabyte",
				"yb_metrics_dir": "/tmp/yugabyte/metrics",
			},
			wantLine: "filename=(/tmp/yugabyte/metrics/node_metrics.prom)",
		},
		{
			name: "defaults yb_metrics_dir to yb_home_dir/metrics",
			values: map[string]any{
				"yb_home_dir": "/home/yugabyte",
			},
			wantLine: "filename=(/home/yugabyte/metrics/node_metrics.prom)",
		},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			output, err := ResolveTemplateStrict(
				context.TODO(),
				tc.values,
				templatePath,
				true, /*strictUndefined*/
			)
			if err != nil {
				t.Fatalf("ResolveTemplateStrict failed: %v", err)
			}
			if !strings.Contains(output, tc.wantLine) {
				t.Fatalf("Expected %q in output:\n%s", tc.wantLine, output)
			}
			if strings.Contains(output, "filename=({{ yb_home_dir }}/metrics/node_metrics.prom)") {
				t.Fatalf("Output still hardcodes yb_home_dir/metrics")
			}
		})
	}
}

func TestSplitString(t *testing.T) {
	values := map[string]any{
		"servers": "s1,s2,s3",
	}
	content := "servers is {{ servers | split_string }}"
	filename := writeTestTemplate(t, content)
	defer os.Remove(filename)
	output, err := ResolveTemplate(
		context.TODO(),
		values,
		filename,
	)
	if err != nil {
		t.Fatalf("Failed to copy file: %v", err)
	}
	expectedOutput := "servers is ['s1', 's2', 's3']"
	if output != expectedOutput {
		t.Fatalf("Unexpected output: %s, found %s", expectedOutput, output)
	}
	t.Logf("Output: %s", output)
}

func TestCustomBooleanTestFunc(t *testing.T) {
	testValues := []struct {
		Input          map[string]any
		ExpectedOutput string
	}{
		{map[string]any{"input": "true"}, " TRUE "},
		{map[string]any{"input": "false"}, " FALSE "},
		{map[string]any{"input": true}, " TRUE "},
		{map[string]any{"input": false}, " FALSE "},
	}
	content := "{% if input is true %} TRUE {% else %} FALSE {% endif %}"
	filename := writeTestTemplate(t, content)
	defer os.Remove(filename)
	for _, value := range testValues {
		output, err := ResolveTemplate(
			context.TODO(),
			value.Input,
			filename,
		)
		if err != nil {
			t.Fatalf("Failed to copy file: %v", err)
		}
		if output != value.ExpectedOutput {
			t.Fatalf("Unexpected output: %s, found %s", value.ExpectedOutput, output)
		}
		t.Logf("Output: %s", output)
	}
}

func TestConvertBoolString(t *testing.T) {
	testValues := []struct {
		Input          map[string]any
		ExpectedOutput string
	}{
		{map[string]any{"string_flag": "true"}, " TRUE "},
		{map[string]any{"string_flag": "false"}, " FALSE "},
	}
	content := "{% if string_flag | bool %} TRUE {% else %} FALSE {% endif %}"
	filename := writeTestTemplate(t, content)
	defer os.Remove(filename)
	for _, value := range testValues {
		output, err := ResolveTemplate(
			context.TODO(),
			value.Input,
			filename,
		)
		if err != nil {
			t.Fatalf("Failed to copy file: %v", err)
		}
		if output != value.ExpectedOutput {
			t.Fatalf("Unexpected output: %s, found %s", value.ExpectedOutput, output)
		}
		t.Logf("Output: %s", output)
	}
}
