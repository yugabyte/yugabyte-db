/*
 * Copyright (c) YugaByte, Inc.
 */

package customcertpath

import (
	"encoding/json"

	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	// EIT1 provides header for EIT cert info
	EIT1 = "table {{.ClientCertPath}}\t{{.ClientKeyPath}}"

	// EIT2 provides header for EIT cert info
	EIT2 = "table {{.NodeCertPath}}\t{{.NodeKeyPath}}"

	// EIT3 provides header for EIT cert info
	EIT3 = "table {{.RootCertPath}}"

	clientCertPathHeader = "Client Certificate Path"
	clientKeyPathHeader  = "Client Key Path"
	nodeCertPathHeader   = "Node Certificate Path"
	nodeKeyPathHeader    = "Node Key Path"
	rootCertPathHeader   = "Root Certificate Path"
)

// EITContext for provider outputs
type EITContext struct {
	formatter.HeaderContext
	formatter.Context
	CustomCertPath ybaclient.CustomCertInfo
}

// NewEITFormat for formatting output
func NewEITFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := EIT1
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// NewEITContext creates a new context for rendering provider
func NewEITContext() *EITContext {
	customcertpathEITCtx := EITContext{}
	customcertpathEITCtx.Header = formatter.SubHeaderContext{
		"ClientCertPath": clientCertPathHeader,
		"ClientKeyPath":  clientKeyPathHeader,
		"NodeCertPath":   nodeCertPathHeader,
		"NodeKeyPath":    nodeKeyPathHeader,
		"RootCertPath":   rootCertPathHeader,
	}
	return &customcertpathEITCtx
}

// ClientCertPath fetches client cert path
func (c *EITContext) ClientCertPath() string {
	return c.CustomCertPath.GetClientCertPath()
}

// ClientKeyPath fetches client key path
func (c *EITContext) ClientKeyPath() string {
	return c.CustomCertPath.GetClientKeyPath()
}

// NodeCertPath fetches node cert path
func (c *EITContext) NodeCertPath() string {
	return c.CustomCertPath.GetNodeCertPath()
}

// NodeKeyPath fetches node key path
func (c *EITContext) NodeKeyPath() string {
	return c.CustomCertPath.GetNodeKeyPath()
}

// RootCertPath fetches root cert path
func (c *EITContext) RootCertPath() string {
	return c.CustomCertPath.GetRootCertPath()
}

// MarshalJSON function
func (c *EITContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.CustomCertPath)
}
