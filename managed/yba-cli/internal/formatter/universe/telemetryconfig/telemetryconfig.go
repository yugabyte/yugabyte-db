/*
 * Copyright (c) YugabyteDB, Inc.
 */

package telemetryconfig

import (
	"encoding/json"

	"github.com/sirupsen/logrus"
	ybav2client "github.com/yugabyte/platform-go-client/v2"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

// NewTelemetryConfigFormat for formatting output. The document only round trips as JSON,
// so table output is coerced.
func NewTelemetryConfigFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		return formatter.Format(formatter.JSONFormatKey)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders a telemetry config document.
func Write(ctx formatter.Context, config *ybav2client.TelemetryConfig) error {
	var output []byte
	var err error
	if ctx.Format.IsPrettyJSON() {
		output, err = json.MarshalIndent(config, "", "  ")
	} else {
		output, err = json.Marshal(config)
	}
	if err != nil {
		logrus.Errorf("Error marshaling telemetry config to json: %v\n", err)
		return err
	}
	if _, err = ctx.Output.Write(output); err != nil {
		return err
	}
	_, err = ctx.Output.Write([]byte("\n"))
	return err
}
