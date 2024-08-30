/*
 * Copyright (c) YugaByte, Inc.
 */

package hashicorp

import (
	"encoding/json"

	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	// EIT provides header for EIT cert info
	EIT = "table {{.Engine}}\t{{.MountPath}}\t{{.Role}}\t{{.VaultAddress}}"

	engineHeader       = "Engine"
	mountPathHeader    = "Mount Path"
	roleHeader         = "Role"
	vaultAddressHeader = "Vault Address"
)

// EITContext for provider outputs
type EITContext struct {
	formatter.HeaderContext
	formatter.Context
	Hashicorp ybaclient.HashicorpVaultConfigParams
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
	hashicorpEITCtx := EITContext{}
	hashicorpEITCtx.Header = formatter.SubHeaderContext{
		"Engine":       engineHeader,
		"MountPath":    mountPathHeader,
		"Role":         roleHeader,
		"VaultAddress": vaultAddressHeader,
	}
	return &hashicorpEITCtx
}

// Engine fetches HCV engine
func (c *EITContext) Engine() string {
	return c.Hashicorp.GetEngine()
}

// MountPath fetches HCV mount path
func (c *EITContext) MountPath() string {
	return c.Hashicorp.GetMountPath()
}

// Role fetches HCV role
func (c *EITContext) Role() string {
	return c.Hashicorp.GetRole()
}

// VaultAddress fetches HCV vault address
func (c *EITContext) VaultAddress() string {
	return c.Hashicorp.GetVaultAddr()
}

// MarshalJSON function
func (c *EITContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.Hashicorp)
}
