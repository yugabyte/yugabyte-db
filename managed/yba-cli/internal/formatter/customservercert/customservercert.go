/*
 * Copyright (c) YugaByte, Inc.
 */

package customservercert

import (
	"encoding/json"

	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	// EIT provides header for EIT cert info
	EIT = "table {{.ServerCert}}\t{{.ServerKey}}"

	serverCertHeader = "Server Certificate"
	serverKeyHeader  = "Server Key"
)

// EITContext for provider outputs
type EITContext struct {
	formatter.HeaderContext
	formatter.Context
	CustomServerCert ybaclient.CustomServerCertInfo
}

// NewEITFormat for formatting output
func NewEITFormat(source string) formatter.Format {
	switch source {
	case "table", "":
		format := EIT
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// NewEITContext creates a new context for rendering provider
func NewEITContext() *EITContext {
	customservercertEITCtx := EITContext{}
	customservercertEITCtx.Header = formatter.SubHeaderContext{
		"ServerCert": serverCertHeader,
		"ServerKey":  serverKeyHeader,
	}
	return &customservercertEITCtx
}

// ServerCert fetches server cert
func (c *EITContext) ServerCert() string {
	return c.CustomServerCert.GetServerCert()
}

// ServerKey fetches server key
func (c *EITContext) ServerKey() string {
	return c.CustomServerCert.GetServerKey()
}

// MarshalJSON function
func (c *EITContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.CustomServerCert)
}
