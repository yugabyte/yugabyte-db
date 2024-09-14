/*
 * Copyright (c) YugaByte, Inc.
 */

package hashicorp

import (
	"encoding/json"
	"fmt"

	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
)

const (
	// EIT provides header for EIT cert info
	EIT = "table {{.Engine}}\t{{.MountPath}}\t{{.Role}}\t{{.VaultAddress}}\t{{.Token}}"

	// EAR1 for HcVaultAuthConfigField listing
	EAR1 = "table {{.Address}}\t{{.Engine}}"

	// EAR2 for HcVaultAuthConfigField listing
	EAR2 = "table {{.MountPath}}\t{{.KeyName}}"

	// EAR3 for HcVaultAuthConfigField listing
	EAR3 = "table {{.Token}}"

	// EAR4 for HcVaultAuthConfigField listing
	EAR4 = "table {{.RoleID}}\t{{.SecretID}}\t{{.AuthNamespace}}"

	// EAR5 for HcVaultAuthConfigField listing
	EAR5 = "table {{.TTL}}\t{{.TTLExpiry}}"

	engineHeader    = "Engine"
	mountPathHeader = "Mount Path"
	roleHeader      = "Role"
	addressHeader   = "Address"

	tokenHeader   = "Token"
	keyNameHeader = "Key Name"

	roleIDHeader        = "Role ID"
	secretIDHeader      = "Secret ID"
	authNamespaceHeader = "Auth Namespace"

	ttlHeader       = "TTL"
	ttlExpiryHeader = "TTL Expiry"
)

// EITContext for provider outputs
type EITContext struct {
	formatter.HeaderContext
	formatter.Context
	Hashicorp ybaclient.HashicorpVaultConfigParams
}

// EARContext for provider outputs
type EARContext struct {
	formatter.HeaderContext
	formatter.Context
	Hashicorp util.HcVaultAuthConfigField
}

// NewEITFormat for formatting output
func NewEITFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := EIT
		return formatter.Format(format)
	default: // custom format or json or pretty
		return formatter.Format(source)
	}
}

// NewEARFormat for formatting output
func NewEARFormat(source string) formatter.Format {
	switch source {
	case formatter.TableFormatKey, "":
		format := EAR1
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
		"VaultAddress": addressHeader,
	}
	return &hashicorpEITCtx
}

// NewEARContext creates a new context for rendering provider
func NewEARContext() *EARContext {
	hashicorpEARCtx := EARContext{}
	hashicorpEARCtx.Header = formatter.SubHeaderContext{
		"Token":         tokenHeader,
		"Address":       addressHeader,
		"Engine":        engineHeader,
		"MountPath":     mountPathHeader,
		"KeyName":       keyNameHeader,
		"RoleID":        roleIDHeader,
		"SecretID":      secretIDHeader,
		"AuthNamespace": authNamespaceHeader,
		"TTL":           ttlHeader,
		"TTLExpiry":     ttlExpiryHeader,
	}
	return &hashicorpEARCtx
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

// Engine fetches HCV engine
func (c *EARContext) Engine() string {
	return c.Hashicorp.HcVaultEngine
}

// MountPath fetches HCV mount path
func (c *EARContext) MountPath() string {
	return c.Hashicorp.HcVaultMountPath
}

// RoleID fetches HCV roleID
func (c *EARContext) RoleID() string {
	return c.Hashicorp.HcVaultRoleID
}

// SecretID fetches HCV secretID
func (c *EARContext) SecretID() string {
	return c.Hashicorp.HcVaultSecretID
}

// AuthNamespace fetches HCV auth namespace
func (c *EARContext) AuthNamespace() string {
	return c.Hashicorp.HcVaultAuthNamespace
}

// Token fetches HCV token
func (c *EARContext) Token() string {
	return c.Hashicorp.HcVaultToken
}

// Address fetches HCV address
func (c *EARContext) Address() string {
	return c.Hashicorp.HcVaultAddress
}

// KeyName fetches HCV key name
func (c *EARContext) KeyName() string {
	return c.Hashicorp.HcVaultKeyName
}

// TTL fetches HCV TTL
func (c *EARContext) TTL() string {
	return fmt.Sprintf("%d", c.Hashicorp.HcVaultTTL)
}

// TTLExpiry fetches HCV TTL expiry
func (c *EARContext) TTLExpiry() string {
	if c.Hashicorp.HcVaultTTL == 0 {
		return "Won't Expire"
	}
	return fmt.Sprintf("%d", c.Hashicorp.HcVaultTTLExpiry)
}

// MarshalJSON function
func (c *EARContext) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.Hashicorp)
}
