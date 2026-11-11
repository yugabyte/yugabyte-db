/*
 * Copyright (c) YugabyteDB, Inc.
 */

package supportbundlev2

import (
	"bytes"
	"encoding/json"
	"reflect"
	"strings"
	"testing"

	ybav2client "github.com/yugabyte/platform-go-client/v2"

	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

func testEstimate() *ybav2client.SupportBundleSizeEstimateResponse {
	data := map[string]map[string]int64{
		"yb-admin-u-n2": {"SystemLogs": 96103401, "CoreFiles": 0},
		"YBA":           {"YbaMetadata": 2612317, "ApplicationLogs": 445864},
		"yb-admin-u-n1": {"SystemLogs": 96295257},
	}
	estimate := ybav2client.NewSupportBundleSizeEstimateResponse()
	estimate.SetData(data)
	return estimate
}

// The API answers with a nested map, which has no stable iteration order. Rows are ordered by
// scope then component so the same estimate renders identically from one run to the next.
func TestFlattenEstimateIsOrdered(t *testing.T) {
	got := FlattenEstimate(testEstimate())
	want := []EstimateRow{
		{Scope: "YBA", Component: "ApplicationLogs", SizeInBytes: 445864},
		{Scope: "YBA", Component: "YbaMetadata", SizeInBytes: 2612317},
		{Scope: "yb-admin-u-n1", Component: "SystemLogs", SizeInBytes: 96295257},
		{Scope: "yb-admin-u-n2", Component: "CoreFiles", SizeInBytes: 0},
		{Scope: "yb-admin-u-n2", Component: "SystemLogs", SizeInBytes: 96103401},
	}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("FlattenEstimate() = %#v, want %#v", got, want)
	}
}

func TestFlattenEstimateHandlesEmptyResponses(t *testing.T) {
	if got := FlattenEstimate(nil); got != nil {
		t.Fatalf("FlattenEstimate(nil) = %#v, want nil", got)
	}
	if got := FlattenEstimate(ybav2client.NewSupportBundleSizeEstimateResponse()); got != nil {
		t.Fatalf("FlattenEstimate(no data) = %#v, want nil", got)
	}
}

// --output table renders a table; the sizes are human readable rather than raw bytes, matching
// the Size column of the bundle listing.
func TestWriteEstimateTable(t *testing.T) {
	out := &bytes.Buffer{}
	ctx := formatter.Context{
		Command: "estimate",
		Output:  out,
		Format:  NewEstimateFormat(formatter.TableFormatKey),
	}
	if err := WriteEstimate(ctx, testEstimate()); err != nil {
		t.Fatalf("WriteEstimate() error = %v", err)
	}

	rendered := out.String()
	for _, want := range []string{
		"Scope", "Component", "Estimated Size",
		"YBA", "ApplicationLogs", "435.41 KB",
		"yb-admin-u-n1", "SystemLogs", "91.83 MB",
		"0.00 bytes",
	} {
		if !strings.Contains(rendered, want) {
			t.Errorf("table output missing %q\ngot:\n%s", want, rendered)
		}
	}
}

// --output json carries the API response unchanged, not the flattened rows, so a script parsing
// the output sees the same shape the server returned.
func TestWriteEstimateJSONKeepsAPIShape(t *testing.T) {
	out := &bytes.Buffer{}
	ctx := formatter.Context{
		Command: "estimate",
		Output:  out,
		Format:  NewEstimateFormat(formatter.JSONFormatKey),
	}
	if err := WriteEstimate(ctx, testEstimate()); err != nil {
		t.Fatalf("WriteEstimate() error = %v", err)
	}

	var decoded map[string]interface{}
	if err := json.Unmarshal(out.Bytes(), &decoded); err != nil {
		t.Fatalf("output is not valid json: %v\ngot: %s", err, out.String())
	}
	data, ok := decoded["data"].(map[string]interface{})
	if !ok {
		t.Fatalf("json output has no data object, got: %s", out.String())
	}
	if len(data) != 3 {
		t.Fatalf("data has %d scopes, want 3: %s", len(data), out.String())
	}
}

// Without the trailing newline the shell prompt lands on the same line as the json.
func TestWriteEstimateJSONEndsWithNewline(t *testing.T) {
	for _, format := range []string{formatter.JSONFormatKey, formatter.PrettyFormatKey} {
		t.Run(format, func(t *testing.T) {
			out := &bytes.Buffer{}
			ctx := formatter.Context{
				Command: "estimate",
				Output:  out,
				Format:  NewEstimateFormat(format),
			}
			if err := WriteEstimate(ctx, testEstimate()); err != nil {
				t.Fatalf("WriteEstimate() error = %v", err)
			}
			if !strings.HasSuffix(out.String(), "\n") {
				t.Fatalf("output does not end with a newline: %q", out.String())
			}
		})
	}
}
