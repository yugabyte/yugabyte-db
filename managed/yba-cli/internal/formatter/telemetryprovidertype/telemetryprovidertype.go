/*
 * Copyright (c) YugabyteDB, Inc.
 */

package telemetryprovidertype

import (
	"encoding/json"

	"github.com/sirupsen/logrus"
	ybav2client "github.com/yugabyte/platform-go-client/v2"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	defaultTelemetryProviderTypeListing = "table {{.ProviderType}}\t{{.AllowedForLogs}}" +
		"\t{{.AllowedForMetrics}}"

	providerTypeHeader      = "Provider Type"
	allowedForLogsHeader    = "Allowed For Logs"
	allowedForMetricsHeader = "Allowed For Metrics"
)

// Context for telemetry provider type outputs
type Context struct {
	formatter.HeaderContext
	formatter.Context
	t ybav2client.TelemetryProviderTypeInfo
}

// NewTelemetryProviderTypeFormat for formatting output
func NewTelemetryProviderTypeFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		return formatter.Format(defaultTelemetryProviderTypeListing)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// Write renders the context for a list of telemetry provider types
func Write(
	ctx formatter.Context,
	providerTypes []ybav2client.TelemetryProviderTypeInfo,
) error {
	if ctx.Format.IsJSON() || ctx.Format.IsPrettyJSON() {
		var output []byte
		var err error
		if ctx.Format.IsPrettyJSON() {
			output, err = json.MarshalIndent(providerTypes, "", "  ")
		} else {
			output, err = json.Marshal(providerTypes)
		}
		if err != nil {
			logrus.Errorf("Error marshaling telemetry provider types to json: %v\n", err)
			return err
		}
		_, err = ctx.Output.Write(output)
		return err
	}
	render := func(format func(subContext formatter.SubContext) error) error {
		for _, providerType := range providerTypes {
			if err := format(&Context{t: providerType}); err != nil {
				logrus.Debugf("Error rendering telemetry provider type: %v", err)
				return err
			}
		}
		return nil
	}
	return ctx.Write(NewTelemetryProviderTypeContext(), render)
}

// NewTelemetryProviderTypeContext creates a new context for rendering provider types
func NewTelemetryProviderTypeContext() *Context {
	ctx := Context{}
	ctx.Header = formatter.SubHeaderContext{
		"ProviderType":      providerTypeHeader,
		"AllowedForLogs":    allowedForLogsHeader,
		"AllowedForMetrics": allowedForMetricsHeader,
	}
	return &ctx
}

// ProviderType returns the provider type name
func (c *Context) ProviderType() string {
	return c.t.GetProviderType()
}

// AllowedForLogs returns whether the provider type supports logs export
func (c *Context) AllowedForLogs() string {
	if c.t.GetIsAllowedForLogs() {
		return "Yes"
	}
	return "No"
}

// AllowedForMetrics returns whether the provider type supports metrics export
func (c *Context) AllowedForMetrics() string {
	if c.t.GetIsAllowedForMetrics() {
		return "Yes"
	}
	return "No"
}

// MarshalJSON function
func (c *Context) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.t)
}
