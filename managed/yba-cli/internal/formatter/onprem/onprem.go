/*
 * Copyright (c) YugaByte, Inc.
 */

package onprem

import (
	"encoding/json"

	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	// Provider provides header for onprem Cloud Info
	Provider = "table {{.YbHomeDir}}"

	ybHomeDirHeader = "YB Home Directory"
)

// ProviderContext for provider outputs
type ProviderContext struct {
	formatter.HeaderContext
	formatter.Context
	Onprem ybaclient.OnPremCloudInfo
}

// NewProviderFormat for formatting output
func NewProviderFormat(source string) formatter.Format {
	switch source {
	case "table", "":
		format := Provider
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// NewProviderContext creates a new context for rendering provider
func NewProviderContext() *ProviderContext {
	onpremProviderCtx := ProviderContext{}
	onpremProviderCtx.Header = formatter.SubHeaderContext{
		"YbHomeDir": ybHomeDirHeader,
	}
	return &onpremProviderCtx
}

// YbHomeDir fetches home directory
func (c *ProviderContext) YbHomeDir() string {
	return c.Onprem.GetYbHomeDir()
}

// MarshalJSON function
func (c *ProviderContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.Onprem)
}
